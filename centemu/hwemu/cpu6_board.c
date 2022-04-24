#include <stdint.h>
#include "logic-common.h"
#include "ginsumatic.h"
#include "am2901.h"
#include "am2909.h"
#include "prom76161.h"

struct cpu6_signals {
	/* System bus connection */
	struct  system_bus {
		word_t A; /* A0-A15 Primary Address lines */
		twobit_t Ax; /* A16-A17? Extended address lines (banking) */
		byte_t D; /* D0-D7 Data lines */
	};
	/* Microcode ROMs */
	struct uROM {
		twelvebit_t uA; /* uA0-uA10 Microcode ROM Address lines */
		byte_t uD[7]; /* uD0-uD55 Microcode ROM Data lines */
		bit_t CE1_, CE2, CE3; /* Chip enable lines, enabled when CE1_=0, CE2=1, CE3=1 */
	} uROM;
	/* Microcode Instruction Word Register */
       	/* Latches last 6 bytes from ROMS with bitsalad=0x76234510 on incoming data lines */
	struct uIWR {
		byte_t uD[6];
		byte_t uQ[6];
	} uIWR;

	struct Seqs {
		twobit_t S[2]; /* Source select (2 bits)*/
		bit_t FE_[2]; /* (Active LO) File enable, HI=Stack HOLD, LO=Push/Pop enabled */
		bit_t PUP[2]; /* Push/Pop select, HI=Push, LO=Pop */
		bit_t Cin,C4,C8,Cout; /* Incrementer Carry-in, Carry-low, Carry-high, Carry-out */
		nibble_t Di[3]; /* Direct Di inputs to MUX */
		bit_t RE_[3]; /* (Active LO) Register Input Enable */
		nibble_t Ri[2]; /* Register direct inputs (am2911 internally ties to Di) */
		nibble_t Y[3]; /* Outputs, when enabled by OE_=0, HiZ when OE_=1 */
		nibble_t ORi[2]; /* Force set bits high (N/A on am2911) */
		bit_t ZERO_[2]; /* (Active LO) Zero all output bits */
		bit_t OE_[3]; /* (Active LO) Enable Y outputs when OE_=0, Tristate when OE_=1 */
	} Seqs;

	struct ALUs {
		octal_t I876, I543, I210; /* ALU instruction word made of three octal pieces */
		nibble_t ADDR_A[2], ADDR_B[2]; /* Internal register selection */
		nibble_t D[2]; /* External data input */
		bit_t Cin,Chalf,Cout; /* Carry-in, Half-carry, Carry-out */
		bit_t Q0[2],Q3[2]; /* Q shifer carry lines */
		bit_t RAM0[2],RAM3[2]; /* RAM shifer carry lines */
		bit_t P_[2], G_[2]; /* (Active LO) Carry lookahead flags */
		bit_t OVR[2]; /* Overflow flags */
		bit_t FZ[2]; /* Zero flags */
		bit_t F3[2]; /* High bit flags, Sign flag if MSB */
		nibble_t Y[2]; /* Outputs (Tri-state when OE_=1) */
		bit_t OE_[2]; /* (Active LO) Output enables */
		byte_t Ycombiner; /* Combined output byte */
	} ALUs;



};


struct cpu6_components {
	/* ROMS */
	union {
		struct { prom76161_device_t UM3,  UL3,  UK3,  UJ3,  UH3,  UF3,  UE3; };
		struct { prom76161_device_t ROM0, ROM1, ROM2, ROM3, ROM4, ROM5, ROM6; };
		struct { prom76161_device_t ROM_E,ROM_D,ROM_A,ROM_F,ROM_C,ROM_B,ROM_M; };
	};

	/* Sequencers */
	union {
		struct { am2909_device_t ULJ10,UL10; am2911_device_t UL9; };
		struct { am2909_device_t SEQ0,SEQ1; am2911_device_t SEQ2; };
	};

	/* ALUs */
	union {
		struct { am2901_device_t UF10,UF11; };
		struct { am2901_device_t ALU0,ALU1; };
	};
};

static char *ROM_files[] = {
	"ROMS/CPU_5.rom", /* MWK3.11 - A3.11 */
	"ROMS/CPU_2.rom", /* MWF3.11 - B3.11 */
	"ROMS/CPU_3.rom", /* MWH3.11 - C3.11 */
	"ROMS/CPU_6.rom", /* MWL3.11 - D3.11 */
	"ROMS/CPU_7.rom", /* MWM3.11 - E3.11 */
	"ROMS/CPU_4.rom", /* MWJ3.11 - F3.11 */
	"ROMS/CPU_1.rom"  /* MWE3.11 - ??3.11 */
};

#define C_(comp) (com.comp)

int main(int argc, char *argv[]) {
	struct cpu6_signals sig;
	struct cpu6_components com;
	clockline_t clockline_uIR, *cl_uIR;
	clockline_t clockline_ALU, *cl_ALU;
	am2901_device_t alu0, alu1;
	am2909_device_t seq0, seq1, seq2;

	byte_t NullByte=0;
	word_t NullWord=0;
	/* Setup bitblenders to combine multiple sources/outputs to a single output */
	byte_ptr_list_t seq_output_list={&sig.Seqs.Y[0], &sig.Seqs.Y[1], &sig.Seqs.Y[2]};
	char *seq_output_order="\x00\x01\x02\x03\xff\xff\xff\xff\x04\x05\x06\x07\xff\xff\xff\xff\x08\x09\x0a\x0b";
	bitblender_t seq_output_blender={{.w=&sig.uROM.uA},11,seq_output_order,&seq_output_list};

	/* Setup clocklines */
	cl_uIR=&clockline_uIR;
	cl_ALU=&clockline_ALU;


	/* Load and initilize 76161 PROMS */
	prom76161_init(&com.ROM_A,ROM_files[0], &sig.uROM.uA, &sig.uROM.uD[0], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_B,ROM_files[1], &sig.uROM.uA, &sig.uROM.uD[1], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_C,ROM_files[2], &sig.uROM.uA, &sig.uROM.uD[2], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_D,ROM_files[3], &sig.uROM.uA, &sig.uROM.uD[3], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_E,ROM_files[4], &sig.uROM.uA, &sig.uROM.uD[4], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_F,ROM_files[5], &sig.uROM.uA, &sig.uROM.uD[5], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);
	prom76161_init(&com.ROM_M,ROM_files[6], &sig.uROM.uA, &sig.uROM.uD[6], &sig.uROM.CE1_, &sig.uROM.CE2, &sig.uROM.CE3);


	/* Initilize two am2909 and one am2911 sequencers */
	am2909_init(&com.SEQ0,"Seq0",&cl_uIR->clk,
		&sig.Seqs.S[0],
		&sig.Seqs.FE_[0], &sig.Seqs.PUP[0],
		&sig.Seqs.Di[0],&sig.Seqs.Ri[0],&sig.Seqs.RE_[0],
		&sig.Seqs.Cin,&sig.Seqs.C4,
		&sig.Seqs.ORi[0],&sig.Seqs.ZERO_[0],&sig.Seqs.OE_[0],&sig.Seqs.Y[0]
	);

	am2909_init(&com.SEQ1,"Seq1",&cl_uIR->clk,
		&sig.Seqs.S[1],
		&sig.Seqs.FE_[1], &sig.Seqs.PUP[1],
		&sig.Seqs.Di[1],&sig.Seqs.Ri[1],&sig.Seqs.RE_[1],
		&sig.Seqs.Cin,&sig.Seqs.C4,
		&sig.Seqs.ORi[1],&sig.Seqs.ZERO_[1],&sig.Seqs.OE_[1],&sig.Seqs.Y[1]
	);

	am2911_init(&com.SEQ2,"Seq2",&cl_uIR->clk,
		&sig.Seqs.S[2],
		&sig.Seqs.FE_[2], &sig.Seqs.PUP[2],
		&sig.Seqs.Di[2],&sig.Seqs.RE_[2],
		&sig.Seqs.Cin,&sig.Seqs.C4,
		&sig.Seqs.ZERO_[2],&sig.Seqs.OE_[2],&sig.Seqs.Y[2]
	);


	/* Initilize the two am2901 ALUs */
	am2901_init(&com.ALU0,"ALU0",&cl_ALU->clk,
		&sig.ALUs.I210, &sig.ALUs.I543, &sig.ALUs.I876,
		&sig.ALUs.RAM0[0], &sig.ALUs.RAM3[0],
		&sig.ALUs.ADDR_A[0], &sig.ALUs.ADDR_B[0], &sig.ALUs.D[0],
		&sig.ALUs.Cin, &sig.ALUs.P_[0], &sig.ALUs.G_[0], &sig.ALUs.Chalf,
		&sig.ALUs.OVR[0], &sig.ALUs.Q0[0], &sig.ALUs.Q3[0],
		&sig.ALUs.FZ[0], &sig.ALUs.F3[0], &sig.ALUs.Y[0], &sig.ALUs.OE_[0]);

	am2901_init(&com.ALU0,"ALU1",&cl_ALU->clk,
		&sig.ALUs.I210, &sig.ALUs.I543, &sig.ALUs.I876,
		&sig.ALUs.RAM0[1], &sig.ALUs.RAM3[1],
		&sig.ALUs.ADDR_A[1], &sig.ALUs.ADDR_B[1], &sig.ALUs.D[1],
		&sig.ALUs.Chalf, &sig.ALUs.P_[1], &sig.ALUs.G_[1], &sig.ALUs.Cout,
		&sig.ALUs.OVR[1], &sig.ALUs.Q0[1], &sig.ALUs.Q3[1],
		&sig.ALUs.FZ[1], &sig.ALUs.F3[1], &sig.ALUs.Y[1], &sig.ALUs.OE_[1]);


	/* Update PROM outputs based on address input and CE1_,CE2,CE3 */
	prom76161_update(&com.ROM_A);
	prom76161_update(&com.ROM_B);
	prom76161_update(&com.ROM_C);
	prom76161_update(&com.ROM_D);
	prom76161_update(&com.ROM_E);
	prom76161_update(&com.ROM_F);
	prom76161_update(&com.ROM_M);

	/* Update uROM address inputs from collected nibbles of output from sequencers */ 
	nibbles_to_word(&sig.uROM.uA, &sig.Seqs.Y[0], &sig.Seqs.Y[1], &sig.Seqs.Y[2], &NullByte);


	nibbles_to_byte(&sig.ALUs.Ycombiner, &sig.ALUs.Y[0], &sig.ALUs.Y[1]);

	return(0);
}
