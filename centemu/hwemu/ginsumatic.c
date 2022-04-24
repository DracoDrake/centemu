/* It slices, it dices... But wait! There's more! */
#include <stdio.h>
#include <inttypes.h>
#include "logic-common.h"
#include "ginsumatic.h"

/* Utility functions to extract ranges of bits, with _R reversing the bit order returned */
#define BITRANGE(d,s,n) ((d>>s) & ((2LL<<(n-1))-1) )
#define BITRANGE_R(d,s,n) (bitreverse_64(BITRANGE(d,s,n)) >>(64-n))


void nibbles_to_byte(uint8_t *dest, nibble_t *n0, nibble_t *n1) {
	*dest=( (*n0)|(*n1<<4) );
}
void nibbles_to_word(uint16_t *dest,
	nibble_t *n0, nibble_t *n1, nibble_t *n2, nibble_t *n3)
{
	*dest=( (*n0)|(*n1<<4)|(*n2<<8)|(*n3<<12) );
}

void nibbles_to_longword( uint32_t *dest,
	nibble_t *n0, nibble_t *n1, nibble_t *n2, nibble_t *n3,
	nibble_t *n4, nibble_t *n5, nibble_t *n6, nibble_t *n7 )
{
	*dest=( (*n0)|(*n1<<4)|(*n2<<8)|(*n3<<12)|(*n4<<16)|(*n5<<20)|(*n6<<24)|(*n7<<28) );
}

void nibbles_to_mouthful( uint64_t *dest,
	nibble_t *n0, nibble_t *n1, nibble_t *n2, nibble_t *n3,
	nibble_t *n4, nibble_t *n5, nibble_t *n6, nibble_t *n7, 
	nibble_t *n8, nibble_t *n9, nibble_t *na, nibble_t *nb,
	nibble_t *nc, nibble_t *nd, nibble_t *ne, nibble_t *nf ) 
{
	*dest=( (*n0)|(*n1<<4)|(*n2<<8)|(*n3<<12)|(*n4<<16)|(*n5<<20)|(*n6<<24)|(*n7<<28) |
		((uint64_t)*n8<<32)|((uint64_t)*n9<<36)|((uint64_t)*na<<40)|((uint64_t)*nb<<44)|
		((uint64_t)*nc<<48)|((uint64_t)*nd<<52)|((uint64_t)*ne<<56)|((uint64_t)*nf<<60) );
}

/* 0x12345678 0x56781234 */
void bitsalad_tosser_8(uint8_t *a, uint8_t *b, uint32_t salad) {
	*b=bitsalad_8(salad,*a);
}

void bitsalad_tosser_16(uint16_t *a, uint16_t *b, uint64_t salad) {
	*b=bitsalad_16(salad,*a);
}


/* Concatenate an array of bytes into an unsigned 64-bit int */
uint64_t concat_bytes_64(uint8_t num, uint8_t bytes[]){
	uint64_t out=0;
	for(int i=0;i<num;i++) {
		out = out | ( ( (uint64_t)bytes[i] )<<(i*8));
	}
	return(out);
}

#define _bitreverse_(size) \
uint##size##_t bitreverse_##size(uint##size##_t in) { \
	uint##size##_t out=0; \
	for(int i=size-1; i>=0; i--) { \
		out |= in&0x1; \
		if(!i){return(out);} \
		out <<=1; in >>=1; \
	}\
	return(out);\
}
_bitreverse_(64);
_bitreverse_(32);
_bitreverse_(16);
_bitreverse_(8);
#undef _bitreverse_
uint64_t xbitreverse_64(uint64_t in) {
	uint64_t out=0;
	for(int i=63;i>=0;i--) {
		out |= in&0x1;
		if(!i){return(out);}
		out <<= 1;
		in >>= 1;
	}
	return(out);
}

uint16_t bitsalad_16(uint64_t order, uint16_t d) {
	uint8_t pos;
	uint16_t out;
	for(int i=0; i<16; i++) {
		pos=BITRANGE(order,i*4,4);
		if(d&1<<i){out |= 1<<pos;}
	}
	return(out);
}

uint8_t bitsalad_8(uint32_t order, uint8_t d) {
	uint8_t pos;
	uint8_t out;
	for(int i=0; i<8; i++) {
		pos=BITRANGE(order,i*4,4);
		if(d&1<<i){out |= 1<<pos;}
	}
	return(out);
}

#define _bitblend_(size) \
uint##size##_t bitblend_##size(uint8_t bits, char *order, uint8_t *source[]) { \
	uint##size##_t out=0; \
	uint8_t pos; \
	char *c; \
	c=order; \
	for(int i=0;i<bits;i++ ) { \
		while( *c > size ) { ; } \
		pos=c-order; \
		if (*(source[pos>>3]) & (1<<(pos&0x3)) ){ out|= (1<<*c); } \
		c++; \
	} \
	return(out);\
}

_bitblend_(8);
_bitblend_(16);
_bitblend_(32);
_bitblend_(64);

void bitblend(bitblender_t *blend) {
	uint8_t pos;
	char *c;
	c=blend->order;
	for(int i=0;i<blend->bits;i++ ) {
	//	printf("\nblending bit %i of %u",i,blend->bits);
		while( (uint8_t)*c > blend->bits - 1 ) { printf("\nskipping c=%u",*c); c++; }
		pos=c-blend->order;
		if (*((*blend->sources)[(pos>>3)]) & (1<<(pos&0x3)) ){
	//		printf("blending bit %i of %u: out=%u, adding found %u at %u: %u\n",
	//				i, blend->bits, *(blend->w),*c,pos, (1<<*c));
			if(blend->bits<9) { *(blend->b) |= (1<<*c); }
			else if(blend->bits<17) { *(blend->w) |= (1<<*c); }
			else if(blend->bits<33) { *(blend->lw) |= (1<<*c); }
			else { *(blend->llw) |= (1LL<<*c); }
		}
		c++;
	}
	return;
}


/* Represent specified number of bits of a 64-bit integer as an ascii string of '1's, '0's and ' ' */
/* Provide an output buffer long enough to hold all bits plus spaces between fields plus NULL */
/* Split sequences of bits into fields of specified widths given as byte string ("\x2\x10\x8..") */
char  *int64_bits_to_binary_string_fields(char *out, uint64_t in, uint8_t bits, char *fieldwidths) {
	char *p;
	char *f;
	uint8_t fc=0;
	f=fieldwidths;
	p=out;
	bits=bits<65?bits:64;
	for(int i=bits-1; i>=0; i--) {
		if(!fc && *f) { fc=*(f++); }
		*(p++)=(in&(1LL<<i))?'1':'0';
		if(!--fc){ *(p++)=' '; }
	}
	*p='\0';
	return(out);
}

/* Represent specified number of bits of a 64-bit integer as an ascii string of '1's and '0's */
/* Provide an output buffer long enough to hold all bits plus spaces between fields plus NULL */
/* Split sequences of bits into space separated groups of specified size */
char  *int64_bits_to_binary_string_grouped(char *out, uint64_t in, uint8_t bits, uint8_t grouping) {
	char *p;
	p=out;
	bits=bits<65?bits:64;
	for(int i=bits-1; i>=0; i--) {
		*(p++)=(in&(1LL<<i))?'1':'0';
		if(i&&!(i%grouping)&&grouping){ *(p++)=' '; }
	}
	*p='\0';
	return(out);
}

int ginsumatic_main() {
	char blendtec[8]="\x00\x01\x02\x03\x07\x06\x05\x04";
	uint8_t in=0x55;
	uint8_t *s[]={&in};
	byte_ptr_list_t t={&in};
	uint8_t out;
	uint16_t out2;
	bitblender_t blend = {{&out}, 8,blendtec,&t};
	uint8_t inA=0x05, inB=0x0a, inC=0x0f;
	byte_ptr_list_t seq_output_list={&inA, &inB, &inC};
	char *seq_output_order="\x00\x01\x02\x03\xff\xff\xff\xff\x04\x05\x06\x07\xff\xff\xff\xff\x08\x09\x0a\x0b";
	bitblender_t seq_output_blender={{.w=&out2},11,seq_output_order,&seq_output_list};
	bitblend(&blend);
	printf("blending %02x -> %02x\n", in, out);
	
	bitblend(&seq_output_blender);
	printf("blending %02x %02x %02x -> %04x\n", inA,inB,inC, out2);
	
	
	printf("blending %02x -> %02x\n", in, bitblend_8(8,blendtec,s));
	return(0);
}