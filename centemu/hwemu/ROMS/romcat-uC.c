#include <stdio.h>
#include <inttypes.h>
#include "../logic-common.h"
#include "../clockline.h"
#include "../ginsumatic.h"
#include "../am2901.h"
#include "../am2909.h"

#define NUMROMS 7
#define ROMSIZE 2048

/* Utility functions to extract ranges of bits, with _R reversing the bit order returned */
#define BITRANGE(d,s,n) ((d>>s) & ((1LL<<(n))-1LL) )
#define BITRANGE_R(d,s,n) (bitreverse_64(BITRANGE(d,s,n)) >>(64-n))

uint8_t allrom[NUMROMS][ROMSIZE];
uint8_t mergedrom[ROMSIZE][NUMROMS];
uint64_t iws[ROMSIZE];
static char *ROM_files[NUMROMS] = {
	"CPU_5.rom", /* MWK3.11 - A3.11 */
	"CPU_2.rom", /* MWF3.11 - B3.11 */
	"CPU_3.rom", /* MWH3.11 - C3.11 */
	"CPU_6.rom", /* MWL3.11 - D3.11 */
	"CPU_7.rom", /* MWM3.11 - E3.11 */
	"CPU_4.rom", /* MWJ3.11 - F3.11 */
	"CPU_1.rom"  /* MWE3.11 - ??3.11 */
};

/*static char *ROM_files[NUMROMS] = {
	"CPU_1.rom",
	"CPU_2.rom",
	"CPU_3.rom",
	"CPU_4.rom",
	"CPU_5.rom",
	"CPU_6.rom",
	"CPU_7.rom"
};
*/

typedef struct cpu_state_t {
	struct dev {
		am2901_device_t ALU0;
		am2901_device_t ALU1;
		am2909_device_t Seq0;
		am2909_device_t Seq1;
		am2911_device_t Seq2;
	} dev;

	struct Shifter {

		bit_t DownLine, UpLine;
	} Shifter;

	struct ALU {
		clockline_t cl;
		octal_t I876, I543, I210;
		nibble_t ADDR_A, ADDR_B;
		nibble_t Dlow, Dhigh;
		bit_t Cin, Chalf, Cout;
		bit_t RAM3, RAM7;
		bit_t RAM0Q7;
		bit_t Q0, Q3;
		bit_t FZ, F3, F7;
		bit_t P_0,G_0, P_1,G_1;
		bit_t nibbleOVF, OVF;
		nibble_t Flow, Fhigh;
		bit_t OE_;
	} ALU;

	struct Bus {
		byte_t Dint;
	} Bus;
} cpu_state_t;


typedef struct uIW_t {
	bit_t S1S1_OVR_;
	twobit_t SHCS;
	nibble_t A,B;
	octal_t I876, I543, I210;
	bit_t CASE_;
	twobit_t S0S, S1S, S2S;
	bit_t PUP, FE_;
	nibble_t D;
	octal_t D_E6,D_E7,D_H11,D_K11;
	nibble_t D_D2D3;
} uIW_t;


typedef struct uIW_trace_t {
	uint16_t addr;
	uIW_t uIW;
	nibble_t Seq0Op, Seq1Op, Seq2Op;
	nibble_t D2_Out;
	sixbit_t D3_Out, E6_Out, K11_Out, H11_Out, E7_Out;
	octal_t S_Shift;
	twobit_t S_Carry;
	
} uIW_trace_t;


enum decoder_enum { D_D2, D_D3, D_E6, D_K11, D_H11, D_E7 };

char *decoded_sig[6][8][2] = {
	{ // Decoder D2 -> latch UD4
		{"D2.0 D4.4",""},
		{"READ REGISTER",""},
		{"READ ADDRESS MSB",""},
		{"D2.3 D4.3",""},
		{"",""},{"",""},
		{"",""},{"",""}
	},
	{ // Decoder D3 -> latch UD5
		{"D3.0 D5.0", ""},
		{"D3.1 D5.1",""},
		{"D3.2 D5.5",""},
		{"READ IL REGISTER",""},
		{"D3.4 D5.2",""},
		{"READ uC DATA CONSTANT",""},
		{"",""},{"",""}

	},
	{ // Decoder E6 (from latch E5)
		{"E6.0",""},
		{"WRITE RESULT REGISTER",""},
		{"WRITE REGISTER SELECT LATCH (RIR)",""},
		{"E6.3",""},
		{"WRITE PAGETABLE BASE ADDRESS REGISTER",""},
		{"STAGING ADDRESS LATCH SOURCE = CURRENT PC","STAGING ADDRESS LATCH SOURCE = DATA"},
		{"WRITE DATA TO SEQUENCERS ADDRESS REGISTER",""},
		{"E6.7",""}
	},
	{ // Decoder K11
		{"K11.0",""},
		{"K11.1",""},
		{"K11.2",""},
		{"K11.3",""},
		{"K11.4",""},
		{"K11.5",""},
		{"K11.6",""},
		{"WRITE DATA REGISTER",""}
	},
	{ // Decoder H11
		{"H11.0",""},
		{"H11.1",""},
		{"H11.2",""},
		{"WRITE ADDRESS LATCH MSB",""},
		{"H11.4",""},
		{"H11.5",""},
		{"READ MAPPING PROM",""},
		{"H11.7",""}
	},
	{ // Decoder E7
		{"E7.0",""},
		{"E7.1",""},
		{"WRITE STATUS REGISTER",""},
		{"E7.3",""},
		{"E7.4",""},
		{"E7.5",""},
		{"E7.6",""},
		{"E7.7",""}
	}

};

char *shifter_ops[8]= {
	"S->RAM7", "?Up1(0?)->RAM7", "Q0->RAM7", "C->RAM7",
	"RAM7->Q0", "?Dn1(0?)->Q0", "S->Q0", "?Dn3(C?)->Q0"
};

char *carry_ops[4]= {
	"?0->Cin", "?1->Cin", "?C->Cin", "?->Cin"
};

int read_roms() {
	FILE *fp;
	size_t ret_code;

	for(int  i=0; i<NUMROMS; i++) {
		fp=fopen(ROM_files[i],"rb");
		ret_code=fread(allrom[i],1,ROMSIZE,fp);
		if(ret_code != ROMSIZE) {
			if(feof(fp)) {
				printf("Unexpected EOF while reading %s: Only got %u byte of an expected %s.\n",
					ROM_files[i], ret_code, ROMSIZE);
			} else if(ferror(fp)){
				printf("Failed while reading %s!\n",ROM_files[i]);
			}
			fclose(fp);
			return(-1);
		}
		fclose(fp);

		//printf("firstval: %x\n",allrom[i][0]);
	}
	return((int)ret_code);

}

int merge_roms() {
	uint64_t iw;
	for(int i=0; i<ROMSIZE; i++) {
		//printf("\n%04x",i);
		for(int j=0; j<NUMROMS; j++) {
			mergedrom[i][j]=allrom[j][i];
			//printf(" %02x",mergedrom[i][j]);
		}
		iw=concat_bytes_64(NUMROMS,mergedrom[i]);
		iws[i]=iw;
		/*
		printf("\tI:%03o A':%x A:%x  %#018"PRIx64"",
			BITRANGE(iw,34,9),
			BITRANGE(iw,44,4),
			BITRANGE_R(iw,44,4),
			iw
		);
		*/
	}

	return(ROMSIZE);
}



void parse_uIW(uIW_t *uIW, uint64_t in) {

	uIW->S1S1_OVR_=BITRANGE(in,54,1); /* Override bit1=1 of S1S when LO, otherwise S1S=S2S */
	uIW->SHCS=BITRANGE(in,51,2); /* Shifter/Carry Control Select */
	uIW->A=BITRANGE(in,47,4); /* ALU A Address (Source) */
	uIW->B=BITRANGE(in,43,4); /* ALU B Address (Source/Dest) */
	uIW->I876=BITRANGE(in,40,3); /* ALU I876 - Destination Control */
	uIW->I543=BITRANGE(in,37,3); /* ALU I543 - Operation */
	uIW->I210=BITRANGE(in,34,3); /* ALU I210 - Operand Source Select */
	uIW->CASE_=BITRANGE(in,33,1); /* Enable conditoinal logic when LO */
	uIW->S2S=BITRANGE(in,31,2); /* Sequencer2 Source Select */
	uIW->S0S=BITRANGE(in,29,2); /* Sequencer0 Source Select */
	uIW->PUP=BITRANGE(in,28,1); /* Sequencers Pop/Push Direction Select */
	uIW->FE_=BITRANGE(in,27,1); /* Sequencers (Push/Pop) File Enable (Active LO) */
	uIW->D=BITRANGE(in,16,11); /* Data */
	uIW->D_E7=BITRANGE(in,13,3); /* Decoder UE7 */
	uIW->D_H11=BITRANGE(in,10,3); /* Decoder UH11 */
	uIW->D_K11=BITRANGE(in,7,3); /* Decoder UK11 */
	uIW->D_E6=BITRANGE(in,4,3); /* Decoder UE6 */
	uIW->D_D2D3=BITRANGE(in,0,4); /* Decoders UD2(bit3=0) (low nibble out) and UD3(bit3=1) (high byte out) */
	
	
}

void uIW_trace_run_ALUs(cpu_state_t *st, uIW_trace_t *t ) {
	st->ALU.I876= t->uIW.I876;
	st->ALU.I543= t->uIW.I543;
	st->ALU.I210= t->uIW.I210;
	st->ALU.ADDR_A= t->uIW.A;
	st->ALU.ADDR_B= t->uIW.B;
	st->ALU.Dlow=  (st->Bus.Dint&0x0f)>>0;
	st->ALU.Dhigh= (st->Bus.Dint&0xf0)>>4;
	st->ALU.Cin= (t->S_Carry==3?0:t->S_Carry==2?st->ALU.Cout:t->S_Carry?1:0);

	if(t->S_Shift&0x4) {
		st->Shifter.UpLine= (t->S_Shift==7?st->ALU.Cout:t->S_Shift==6?st->ALU.F7:t->S_Shift==5?0:st->ALU.RAM7);
		st->ALU.Q0=st->Shifter.UpLine;
	} else {
		st->Shifter.DownLine= (t->S_Shift==3?st->ALU.Cout:t->S_Shift==2?st->ALU.Q0:t->S_Shift?0:st->ALU.F7);
		st->ALU.RAM7=st->Shifter.DownLine;
	}

	st->ALU.cl.clk=CLK_LO;
	do{
		am2901_update(&st->dev.ALU0);
		am2901_update(&st->dev.ALU1);
		clock_advance(&st->ALU.cl);
	} while(st->ALU.cl.clk);
}

void trace_uIW(cpu_state_t *cpu_st, uIW_trace_t *t, uint16_t addr, uint64_t in) {
	t->addr=addr;
	parse_uIW(&(t->uIW), in);

	/* Fill in S1S from logic on S2S and S1S1_OVR_ signals */
	/* S1S0 = S2S0, S1S1 = S2S1 -> INV -> NAND S1S1_OVR_ */
	/* S1S =    NAND( INV(S2S1),                 S1S1_OVR_<<1       ) OR     S1S0 */
	t->uIW.S1S= ( ~( (~t->uIW.S2S&0x2) & (t->uIW.S1S1_OVR_<<1) )&0x2) | ((t->uIW.S2S)&0x1) ;

	/* Assemble complete sequencer operations for each sequencer */
	/* S0&S2 S{01} -> (NAND INT_), FE_ -> (NAND INT_) -> INV (cancels), PUP has no NAND */
	t->Seq0Op=  ( ((~t->uIW.S0S)&0x3)<<2) | (t->uIW.FE_<<1) | (t->uIW.PUP<<0) ;
	t->Seq1Op=  ( ((~t->uIW.S1S)&0x3)<<2) | (t->uIW.FE_<<1) | (t->uIW.PUP<<0) ;
	t->Seq2Op=  ( ((~t->uIW.S2S)&0x3)<<2) | (t->uIW.FE_<<1) | (t->uIW.PUP<<0) ;



	/* Decode decoder codes (Active LO) */
	t->D2_Out= ~(  ( t->uIW.D_D2D3&0x8)? 0 : 1<<((t->uIW.D_D2D3)&0x3) )&0xf;
	t->D3_Out= ~( (t->uIW.D_D2D3&0x8)? 1<<((t->uIW.D_D2D3)&0x7) : 0 )&0x3f;
	t->E6_Out= ~( 1<<(t->uIW.D_E6) )&0xff;
	t->K11_Out= ~( 1<<(t->uIW.D_K11) )&0xff;
	t->H11_Out= ~( 1<<(t->uIW.D_H11) )&0xff;
	t->E7_Out= ~( 1<<(t->uIW.D_E7) )&0xff;

	/* UF6 - Carry Control *
	 * Select from 
	 * Output enables driven by ?
	 * Carry control input connections:
	 *                         SHCS=0    SHCS=1     SHCS=2    SHCS=3
	 *     ??=0: (Za Enabled)  I0a=?     I1a=?      I2a=?     I3a=?
	 *     ??=1: (Zb Enabled)  I0b=?     I1b=?      I2b=?     I3b=?
	 *
	 * Carry control output connections:
	 *  Za -> ALU0.Cn (UF10)
	 *
	 *  0,0 -> ? Zero?
	 *  0,1 -> ? One?
	 *  0,2 -> ? Carry?
	 *  0,3 -> ? ??
	 */
	t->S_Carry=t->uIW.SHCS;


	/* UH6 - Shift Control *
	 * Select from 
	 * Output enables driven by ALU.I7 -> OEa_, -> INV -> OEb_
	 * ALU.I7=0 -> Shift Down, ALU.I7=1 Shift Up
	 * Shifter input connections:
	 *                         SHCS=0     SHCS=1     SHCS=2     SHCS=3
	 * ALU.I7=0: (Za Enabled)  I0a=S(2b)  I1a=?(1b)  I2a=Q0     I3a=C
	 * ALU.I7=1: (Zb Enabled)  I0b=RAM7   I1b=?(1a)  I2b=S(0a)  I3b=
	 *
	 * Shifter output connections:
	 * Za -> RAM7[ALU1.RAM3] (UF11), UJ10.I5
	 * Zb -> Q0[ALU0.Q0] (UF10), UJ10.I7
	 *
	 * 0,0:    S->RAM7  SRA (Sign extend S->MSB)
	 * 0,1:    ?->RAM7  SRL? (Zero?)
	 * 0,2:   Q0->RAM7  (SRA/RRR) Half-word (Q0 of High byte shifts into MSB of Low byte)
	 * 0,3:    C->RAM7  RRR (Rotate carry into MSB)
	 *
	 * 1,0: RAM7->Q0    RLR/SLR Half-word? (RAM7->Q0)
	 * 1,1:    ?->Q0    SLR? (Zero?)
	 * 1,2:    S->Q0    ?
	 * 1,3:    ?->Q0    (C?) RLR?
	 *
	 *
	 * See microcode @ 0x0d2-0x0e2 for 16 bit shift up through Q
	 */
	t->S_Shift=(t->uIW.I876&0x2?4:0)|t->uIW.SHCS;

	uIW_trace_run_ALUs(cpu_st, t);
	
}

void init_ALUs(cpu_state_t *st) {
	am2901_init(&st->dev.ALU0, "ALU0", &st->ALU.cl.clk,
		&st->ALU.I210, &st->ALU.I543, &st->ALU.I876,
		&st->ALU.RAM0Q7, &st->ALU.RAM3,
		&st->ALU.ADDR_A, &st->ALU.ADDR_B,
		&st->ALU.Dlow, &st->ALU.Cin,
		&st->ALU.P_0, &st->ALU.G_0,
		&st->ALU.Chalf, &st->ALU.nibbleOVF,
		&st->ALU.Q0, &st->ALU.Q3,
		&st->ALU.FZ, &st->ALU.F3,
		&st->ALU.Flow, &st->ALU.OE_);

	am2901_init(&st->dev.ALU1, "ALU1", &st->ALU.cl.clk,
		&st->ALU.I210, &st->ALU.I543, &st->ALU.I876,
		&st->ALU.RAM3, &st->ALU.RAM7,
		&st->ALU.ADDR_A, &st->ALU.ADDR_B,
		&st->ALU.Dhigh, &st->ALU.Chalf,
		&st->ALU.P_1, &st->ALU.G_1,
		&st->ALU.Cout, &st->ALU.OVF,
		&st->ALU.Q3, &st->ALU.RAM0Q7,
		&st->ALU.FZ, &st->ALU.F7,
		&st->ALU.Fhigh, &st->ALU.OE_);
}



void print_decoder_values(enum decoder_enum d, uint8_t v) {
	octal_t p;
	char *s;
	for(int i=0; i<8; i++) {
		p=(v>>i)&0x1;
		s=decoded_sig[d][i][p];
		printf("%s%s%s",(!p&&*s)?"_":"",s,*s? p?"\n":"_\n":"");
	}
}

void print_uIW_trace(uIW_trace_t *t) {
	printf("Data: D/uADDR=%#05x (D_=%#04x)", t->uIW.D, (~t->uIW.D)&0xff);
	printf(" Shifter: %s / Carry Select: %s (SHCS=%0x)\n",shifter_ops[t->S_Shift], carry_ops[t->S_Carry], t->uIW.SHCS);
	printf("ALUs: A=%01x B=%01x RS=%s %s -> %s\n",
		t->uIW.A,
		t->uIW.B,
		am2901_source_operand_mnemonics[t->uIW.I210],
		am2901_function_symbol[t->uIW.I543],
		am2901_destination_mnemonics[t->uIW.I876]
	);
	printf("Seqs: S0: %s, S1: %s, S2: %s\n",
		am2909_ops[t->Seq0Op][3],
		am2909_ops[t->Seq1Op][3],
		am2909_ops[t->Seq2Op][3]
	);
	printf("\nDecoders E7:%02x H11: %02x K11: %02x E6: %02x D3: %02x D2 %02x\n",
		t->E7_Out, t->H11_Out, t->K11_Out, t->E6_Out, t->D3_Out, t->D2_Out);

	print_decoder_values(D_D2,  t->D2_Out);
	print_decoder_values(D_D3,  t->D3_Out);
	print_decoder_values(D_E6,  t->E6_Out);
	print_decoder_values(D_K11, t->K11_Out);
	print_decoder_values(D_H11, t->H11_Out);
	print_decoder_values(D_E7,  t->E7_Out);
	printf("\n");
		

}

void init_cpu_state(cpu_state_t *st) {
	clock_init(&st->ALU.cl,"ALU Clock", CLK_LO);
	init_ALUs(st);
}

int main(int argc, char **argv) {
	int r;
	uint16_t tmp;
	uint64_t salad;
	char binstr[100];
	cpu_state_t cpu_st;
	uIW_trace_t trace[ROMSIZE];
	if( (r=read_roms()) > 0 ) {
		printf("read_roms returned: %i\n",r);
		merge_roms();

		init_cpu_state(&cpu_st);

		for(int i=0; i<ROMSIZE; i++) {
			//printf("\n%#06x: %#06x",i,allrom[0][i]);
			//byte_bits_to_binary_string_grouped(binstr, allrom[0][i], NUMROMS*8, 1);
			//printf("   %s",binstr);
			//int64_bits_to_binary_string_grouped(binstr, iws[i], NUMROMS*8,4);
			int64_bits_to_binary_string_fields(binstr, iws[i], NUMROMS*8,
				"\x1\1\1\x2\x4\x4\x3\x3\x3\x1\x2\x2\x2\x3\x4\x4\x3\x3\x3\x3\x4");
			printf(" %#06x: %#018"PRIx64" %s\n",i,iws[i],binstr);
			trace_uIW(&cpu_st, &trace[i],i,iws[i]);
			print_uIW_trace(&trace[i]);
		/*	
			printf(" 2901:");
			printf(" I=%03o",BITRANGE(iws[i],34,9));
			printf(" A=%01x",BITRANGE(iws[i],47,4));
			printf(" B=%01x",BITRANGE(iws[i],43,4));
			printf(" 2909:");
			printf(" PUP/FE=%01o",BITRANGE(iws[i],27,2));
			printf(" S0S=%01o",BITRANGE(iws[i],29,2));
			printf(" (S1S=%01o)",BITRANGE(iws[i],31,2)|(BITRANGE(iws[i],54,1)?2:0));
			printf(" S2S=%01o",BITRANGE(iws[i],31,2));
			printf(" D=%03x",BITRANGE(iws[i],16,11));
			printf("\n");
		*/
		}
	} else {
		printf("Could not read ROMS!");
	}
}
