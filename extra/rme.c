/*
 * Realmode Emulator Plugin
 * - By John Hodge (thePowersGang)
 *
 * This code is published under the FreeBSD licence
 * (See the file COPYING for details)
 *
 * ---
 * Core Emulator
 */
#define _RME_C_
#include <stdint.h>
//#include <stdlib.h>
//#include <stdio.h>

//#include <common.h>

typedef uint16_t	Uint16;
typedef int16_t	Sint16;
typedef int8_t	Sint8;

#include "rme.h"
#include "ops_alu.h"
#include "rme_internal.h"

// Settings
#define RME_DO_NULL_CHECK	1
#define	printf	printf	// Formatted print function

// === CONSTANTS ===
#define FLAG_DEFAULT	0x2

// === MACRO VOODOO ===
#define XCHG(a,b)	do{uint32_t t=(a);(a)=(b);(b)=(t);}while(0)

// === TYPES ===
typedef int (*tOpcodeFcn)(tRME_State *State, int Param);

// === PROTOTYPES ===
tRME_State	*RME_CreateState(void);
void	RME_DumpRegs(tRME_State *State);
 int	RME_CallInt(tRME_State *State, int Num);
 int	RME_Call(tRME_State *State);
static int	RME_Int_DoOpcode(tRME_State *State);

// === GLOBALS ===
#include "opcode_table.h"

// === CODE ===
/**
 * \brief Creates a blank RME State
 */
tRME_State *RME_CreateState(void)
{
	tRME_State	*state = calloc(sizeof(tRME_State), 1);

	if(state == NULL)	return NULL;

	state->DebugLevel = DEBUG;
	// Initial Stack
	state->Flags = FLAG_DEFAULT;

	// Stub CS/IP
	state->CS = 0xF000;
	state->IP = 0xFFF0;

	return state;
}

/**
 * \brief Dump Realmode Registers
 */
void RME_DumpRegs(tRME_State *State)
{
	printf("\n");
	#if USE_SIZE_OVERRIDES == 1
	printf("EAX %08x  ECX %08x  EDX %08x  EBX %08x\n",
		State->AX.D, State->CX.D, State->DX.D, State->BX.D);
	printf("ESP %08x  EBP %08x  ESI %08x  EDI %08x\n",
		State->SP.D, State->BP.D, State->SI.D, State->DI.D);
	#else
	printf("AX %04x  CX %04x  DX %04x  BX %04x\n",
		State->AX.W, State->CX.W, State->DX.W, State->BX.W);
	printf("SP %04x  BP %04x  SI %04x  DI %04x\n",
		State->SP.W, State->BP.W, State->SI.W, State->DI.W);
	#endif
	printf("SS %04x  DS %04x  ES %04x\n",
		State->SS, State->DS, State->ES);
	printf("CS:IP = 0x%04x:%04x\n", State->CS, State->IP);
	printf("Flags = %04x", State->Flags);
	if(State->Flags & FLAG_OF)	printf(" OF");
	if(State->Flags & FLAG_DF)	printf(" DF");
	if(State->Flags & FLAG_IF)	printf(" IF");
	if(State->Flags & FLAG_TF)	printf(" TF");
	if(State->Flags & FLAG_SF)	printf(" SF");
	if(State->Flags & FLAG_ZF)	printf(" ZF");
	if(State->Flags & FLAG_AF)	printf(" AF");
	if(State->Flags & FLAG_PF)	printf(" PF");
	if(State->Flags & FLAG_CF)	printf(" CF");
	printf("\n");
}

int RME_int_CallInt(tRME_State *State, int Num)
{
	 int	ret;
	if(State->DebugLevel > 0) {
		printf("RM_Int: Calling Int 0x%x\n", Num);
	}

	if(Num < 0 || Num > 0xFF) {
		ERROR_S("WARNING: %i is not a valid interrupt number", Num);
		return RME_ERR_INVAL;
	}

	PUSH(State->Flags);
	PUSH(State->CS);
	PUSH(State->IP);
	State->Flags &= ~(FLAG_IF|FLAG_TF);

	ret = RME_Int_Read16(State, 0, Num*4, &State->IP);
	if(ret)	return ret;
	ret = RME_Int_Read16(State, 0, Num*4+2, &State->CS);
	if(ret)	return ret;
	
	return 0;
}

/**
 * \brief Run Realmode interrupt
 */
int RME_CallInt(tRME_State *State, int Num)
{
	 int	ret;

	State->CS = RME_MAGIC_CS;	
	State->IP = RME_MAGIC_IP;

	ret = RME_int_CallInt(State, Num);
	if(ret)	return ret;

	return RME_Call(State);
}

/**
 * \brief Call a realmode function (a jump to a magic location is used as the return)
 */
int RME_Call(tRME_State *State)
{
	 int	ret;
	for(;;)
	{
		ret = RME_RunOne(State);
		if( ret == RME_ERR_FCNRET )
			return 0;
		if( ret != RME_ERR_OK )
			return ret;
	}
}

/*
 * \brief Run one instruction (or HLE operation)
 */
int RME_RunOne(tRME_State *State)
{
	if(State->DebugLevel >= 2) {
		RME_DumpRegs(State);
	}
	
	if(State->IP == RME_MAGIC_IP && State->CS == RME_MAGIC_CS)
		return RME_ERR_FCNRET;
	
	if(State->CS == RME_HLE_CS && State->IP < 0x100) {
		// HLE Call
		if( State->HLECallbacks[State->IP] )
			State->HLECallbacks[State->IP](State, State->IP);
		// IRET
		caOperations[0xCF].Function(State, 0);
		return 0;
	}
	
	int ret = RME_Int_DoOpcode(State);
	switch(ret)
	{
	case RME_ERR_OK:
		break;
	case RME_ERR_DIVERR:
		ret = RME_int_CallInt(State, 0);
		if(ret)	return ret;
		break;
	// TODO: Handle #UD and others here
	default:
		return ret;
	}
	
	return 0;
}

/**
 * \brief Processes a single instruction
 */
int RME_Int_DoOpcode(tRME_State *State)
{
	 uint8_t	opcode;	// Current opcode and second byte
	 int	ret;	// Return value from functions
	uint16_t	startIP, startCS;	// Initial CPU location

	startIP = State->IP;
	startCS = State->CS;

	State->Decoder.OverrideSegment = -1;
	State->Decoder.RepeatType = 0;
	State->Decoder.bOverrideOperand = 0;
	State->Decoder.bOverrideAddress = 0;
	State->Decoder.bDontChangeIP = 0;
	State->Decoder.IPOffset = 0;
	State->Decoder.DebugStringLen = 0;
	State->Decoder.DebugString[0] = 0;
	State->InstrNum ++;

	if( State->DebugLevel > 0 ) {
		printf("(%8i) [0x%x] %04x:%04x", State->InstrNum, State->CS*16+State->IP, State->CS, State->IP);
	}

	do
	{
		READ_INSTR8( opcode );
		// HACK 0xF1 is blank in the x86 opcode map, so it's used as uninit padding
		if( opcode == 0xF1 ) {
			ERROR_S(" Executing unset memory (opcode 0xF1) %04x:%04x",
				State->CS, State->IP);
			return 7;
		}
		if( caOperations[opcode].Function == NULL )
		{
			ERROR_S(" Unkown Opcode 0x%02x", opcode);
			return RME_ERR_UNDEFOPCODE;
		}
		
		const char* name;
		struct ModRM	modrm;
		if(caOperations[opcode].ModRMNames) {
			RME_Int_GetModRM(State, &modrm);
			State->Decoder.IPOffset --;
			name = caOperations[opcode].ModRMNames[modrm.rrr];
			//assert(caOperations[opcode].Arg == 0);
		}
		else {
			name = caOperations[opcode].Name;
		}
		if(State->Decoder.DebugStringLen)	RME_Int_DebugPrint(State, " ");
		RME_Int_DebugPrint(State, "%s (%s)", name, caOperations[opcode].Type);
		ret = caOperations[opcode].Function(State, caOperations[opcode].ModRMNames ? modrm.rrr : caOperations[opcode].Arg);
	} while( ret == RME_ERR_CONTINUE );	// RME_ERR_CONTINUE is returned by prefixes
	
	if( ret )
		return ret;

	// repType is cleared if it is used, so if it's not used, it's invalid
	if(State->Decoder.RepeatType)
	{
		if( State->DebugLevel > 0 ) {
			printf(" Prefix 0x%02x used with wrong opcode 0x%02x", State->Decoder.RepeatType, opcode);
		}
		// - Legal, but definitely not intentional
		//return RME_ERR_UNDEFOPCODE;
	}

	if( !State->Decoder.bDontChangeIP )
		State->IP += State->Decoder.IPOffset;

	// HACK: Detect more than one instance of 00 00 in a row
	#if 1
	if( State->Decoder.IPOffset == 2 ) {
		uint8_t	byte1=0xFF, byte2=0xFF;
		ret = RME_Int_Read8(State, startCS, startIP+0, &byte1);
		if(ret)	return ret;
		ret = RME_Int_Read8(State, startCS, startIP+1, &byte2);
		if(ret)	return ret;
		
		if( byte1 == 0 && byte2 == 0 )
		{
			if( State->bWasLastOperationNull )
				return RME_ERR_BREAKPOINT;
			State->bWasLastOperationNull = 1;
		}
		else
			State->bWasLastOperationNull = 0;
	}
	else
		State->bWasLastOperationNull = 0;
	#endif

	if( State->DebugLevel >= 1 )
	{
		uint16_t i = startIP;
		uint8_t	byte;
		 int	j = State->Decoder.IPOffset;

		printf(" %s", State->Decoder.DebugString);
		printf("\t;");
		while(j--) {
			ret = RME_Int_Read8(State, startCS, i, &byte);
			if(ret)	return ret;
			printf(" %02x", byte);
			i ++;
		}
		printf("\n");
	}
	return 0;
}

DEF_OPCODE_FCN(Ext,0F)
{
	uint8_t	extra;
	READ_INSTR8(extra);
	
	if( caOperations0F[extra].Function == NULL )
	{
		ERROR_S(" Unkown Opcode 0x0F 0x%02x", extra);
		return RME_ERR_UNDEFOPCODE;
	}
	
	RME_Int_DebugPrint(State, " %s", caOperations0F[extra].Name);
	return caOperations0F[extra].Function(State, caOperations0F[extra].Arg);
}

DEF_OPCODE_FCN(Unary, M)	// INC/DEC r/m8
{
	const int	width = 8;
	 int	ret;
	uint8_t	*dest;
	
	switch(Param)
	{
	case 0:	// INC
		ret = RME_Int_ParseModRM(State, NULL, &dest, 0);
		if(ret)	return ret;
		{ALU_OPCODE_INC_CODE}
		break;
	case 1:	// DEC
		ret = RME_Int_ParseModRM(State, NULL, &dest, 0);
		if(ret)	return ret;
		{ALU_OPCODE_DEC_CODE}
		break;
	default:
		ERROR_S(" - Unary M /%i unimplemented\n", Param);
		return RME_ERR_UNDEFOPCODE;
	}
	
	return 0;
}

int RME_Int_ParseModRMX_FarPtr(tRME_State* State, uint16_t* cs, uint16_t* ip)
{
	int ret;

	struct ModRM modrm;
	ret = RME_Int_GetModRM(State, &modrm);
	if(ret)	return ret;

	if(modrm.mod == 3) {
		ERROR_S(" - Reading a far pointer w/ mod=3");
		return RME_ERR_UNDEFOPCODE;
	}

	uint16_t	segment;
	uint32_t	offset;
	ret = RME_Int_GetMMM( State, &modrm, &segment, &offset );
	if(ret)	return ret;

	ret = RME_Int_Read16(State, segment, offset, ip);
	if(ret) return ret;
	ret = RME_Int_Read16(State, segment, offset+2, cs);
	if(ret) return ret;

	return 0;
}

DEF_OPCODE_FCN(Unary, MX)	// INC/DEC r/m16, CALL/JMP/PUSH r/m16
{
	 int	ret;

	if( State->Decoder.bOverrideOperand )
	{
		uint32_t	*dest;
		const int	width = 32;	
		switch( Param )
		{
		case 0:
			ret = RME_Int_ParseModRMX(State, NULL, (void*)&dest, 0);
			if(ret)	return ret;
			{ALU_OPCODE_INC_CODE}
			SET_COMM_FLAGS(State, *dest, width);
			break;
		case 1:
			ret = RME_Int_ParseModRMX(State, NULL, (void*)&dest, 0);
			if(ret)	return ret;
			{ALU_OPCODE_DEC_CODE}
			SET_COMM_FLAGS(State, *dest, width);
			break;
		case 6:
			ret = RME_Int_ParseModRMX(State, NULL, (void*)&dest, 0);	//Get Register Value
			if(ret)	return ret;
			PUSH( *dest );
			break;
		default:
			ERROR_S(" - Unary MX (32) /%i unimplemented\n", Param);
			return RME_ERR_UNDEFOPCODE;
		}
	}
	else
	{
		uint16_t cs, ip;
		uint16_t	*dest;
		const int	width = 16;	
		switch( Param )
		{
		case 0:	// INC
			ret = RME_Int_ParseModRMX(State, NULL, &dest, 0);
			if(ret)	return ret;
			{ALU_OPCODE_INC_CODE}
			SET_COMM_FLAGS(State, *dest, width);
			break;
		case 1:	// DEC
			ret = RME_Int_ParseModRMX(State, NULL, &dest, 0);
			if(ret)	return ret;
			{ALU_OPCODE_DEC_CODE}
			SET_COMM_FLAGS(State, *dest, width);
			break;
		case 2:	// Call Near Indirect
			ret = RME_Int_ParseModRMX(State, NULL, &dest, 0);
			if(ret)	return ret;
			PUSH(State->IP + State->Decoder.IPOffset);
			State->IP = *dest;
			State->Decoder.bDontChangeIP = 1;
			break;
		case 3:	// Call Far Indirect
			ret = RME_Int_ParseModRMX_FarPtr(State, &cs, &ip);
			if(ret) return ret;
			PUSH(State->CS);
			PUSH(State->IP + State->Decoder.IPOffset);
			State->IP = ip;
			State->CS = cs;
			State->Decoder.bDontChangeIP = 1;
			break;
		case 4:	// Jump Near Indirect
			ret = RME_Int_ParseModRMX(State, NULL, &dest, 0);
			if(ret)	return ret;
			State->IP = *dest;
			State->Decoder.bDontChangeIP = 1;
			break;
		case 5:	// Jump Far Indirect
			ret = RME_Int_ParseModRMX_FarPtr(State, &cs, &ip);
			if(ret) return ret;
			if(ret)	return ret;
			State->IP = ip;
			State->CS = cs;
			State->Decoder.bDontChangeIP = 1;
			break;
		case 6:	// Push
			ret = RME_Int_ParseModRMX(State, NULL, &dest, 0);
			if(ret)	return ret;
			PUSH( *dest );
			break;
			
		default:
			ERROR_S(" - Unary MX (16) /%i unimplemented\n", Param);
			return RME_ERR_UNDEFOPCODE;
		}
	}
	
	return 0;
}

// =====================================================================
//                 ModR/M and SIB Addressing Helpers
// =====================================================================
/**
 * \brief Performs a memory addressing function
 * \param State	Emulator State
 * \param mmm	Function ID (mmm field from ModR/M byte)
 * \param disp	Displacement
 * \param ptr	Destination for final pointer
 */
static int DoFunc(tRME_State *State, int mmm, int16_t disp, uint16_t *Segment, uint32_t *Offset)
{
	uint32_t	addr;
	uint16_t	seg;

	switch(mmm){
	case 2:	case 3:	case 6:
		seg = SREG_SS;
		break;
	default:
		seg = SREG_DS;
		break;
	}

	if(State->Decoder.OverrideSegment != -1)
		seg = State->Decoder.OverrideSegment;

	seg = *Seg(State, seg);

	RME_Int_DebugPrint(State, ":[");
	switch(mmm & 7)
	{
	case 0:
		RME_Int_DebugPrint(State, "BX+SI");
		addr = State->BX.W + State->SI.W;
		break;
	case 1:
		RME_Int_DebugPrint(State, "BX+DI");
		addr = State->BX.W + State->DI.W;
		break;
	case 2:
		RME_Int_DebugPrint(State, "BP+SI");
		addr = State->BP.W + State->SI.W;
		break;
	case 3:
		RME_Int_DebugPrint(State, "BP+DI");
		addr = State->BP.W + State->DI.W;
		break;
	case 4:
		RME_Int_DebugPrint(State, "SI");
		addr = State->SI.W;
		break;
	case 5:
		RME_Int_DebugPrint(State, "DI");
		addr = State->DI.W;
		break;
	case 6:
		if( mmm & 8 ) {
			READ_INSTR16( disp );
			RME_Int_DebugPrint(State, "0x%04x", disp);
			addr = disp;
		}
		else {
			RME_Int_DebugPrint(State, "BP");
			addr = State->BP.W;
		}
		break;
	case 7:
		RME_Int_DebugPrint(State, "BX");
		addr = State->BX.W;
		break;
	default:
		ERROR_S("Unknown mmm value passed to DoFunc (%i)", mmm);
		return RME_ERR_BUG;
	}
	if( !(mmm & 8) ) {
		RME_Int_DebugPrint(State, "+0x%x", disp);
		addr += disp;
	}
	RME_Int_DebugPrint(State, "]");
	*Segment = seg;
	*Offset = addr;
	return 0;
}

/**
 * \brief Performs a memory addressing function (32-bit, with SIB if nessesary)
 * \param State	Emulator State
 * \param mmm	Function ID (mmm field from ModR/M byte)
 * \param disp	Displacement
 * \param ptr	Destination for final pointer
 */
static int DoFunc32(tRME_State *State, int mmm, int32_t disp, uint16_t *Segment, uint32_t *Offset)
{
	uint32_t	addr;
	uint16_t	seg;
	uint8_t	sib;

	switch(mmm){
	case 2:	case 3:	case 6:
		seg = SREG_SS;
		break;
	default:
		seg = SREG_DS;
		break;
	}

	if(State->Decoder.OverrideSegment != -1)
		seg = State->Decoder.OverrideSegment;

	seg = *Seg(State, seg);

	RME_Int_DebugPrint(State, ":[");
	switch(mmm & 7)
	{
	case 0:
		RME_Int_DebugPrint(State, "EAX");
		addr = State->AX.D;
		break;
	case 1:
		RME_Int_DebugPrint(State, "ECX");
		addr = State->CX.D;
		break;
	case 2:
		RME_Int_DebugPrint(State, "EDX");
		addr = State->DX.D;
		break;
	case 3:
		RME_Int_DebugPrint(State, "EBX");
		addr = State->BX.D;
		break;
	case 4:	// SIB (uses ESP's slot)
		READ_INSTR8(sib);
		addr = 0;
		// Index Reg
		switch( (sib >> 3) & 7 )
		{
		case 0:	RME_Int_DebugPrint(State, "EAX");	addr = State->AX.D;	break;
		case 1:	RME_Int_DebugPrint(State, "ECX");	addr = State->CX.D;	break;
		case 2:	RME_Int_DebugPrint(State, "EDX");	addr = State->DX.D;	break;
		case 3:	RME_Int_DebugPrint(State, "EBX");	addr = State->BX.D;	break;
		case 4:	RME_Int_DebugPrint(State, "EIZ");	addr = 0;	break;
		case 5:	RME_Int_DebugPrint(State, "EBP");	addr = State->BP.D;	break;
		case 6:	RME_Int_DebugPrint(State, "ESI");	addr = State->SI.D;	break;
		case 7:	RME_Int_DebugPrint(State, "EDI");	addr = State->DI.D;	break;
		}
		// Scale
		RME_Int_DebugPrint(State, "*%i", 1 << (sib >> 6));
		addr <<= (sib >> 6);
		// Base
		switch( sib & 7 )
		{
		case 0:	RME_Int_DebugPrint(State, "+EAX");	addr += State->AX.D;	break;
		case 1:	RME_Int_DebugPrint(State, "+ECX");	addr += State->CX.D;	break;
		case 2:	RME_Int_DebugPrint(State, "+EDX");	addr += State->DX.D;	break;
		case 3:	RME_Int_DebugPrint(State, "+EBX");	addr += State->BX.D;	break;
		case 4:	RME_Int_DebugPrint(State, "+ESP");	addr += State->SP.D;	break;
		case 5:	// SPECIAL CASE
			if( mmm & 8 ) {
				READ_INSTR32(disp);
			}
			else
			{
				RME_Int_DebugPrint(State, "+EBP");
				addr += State->BP.D;
			}
			break;
		case 6:	RME_Int_DebugPrint(State, "+ESI");	addr += State->SI.D;	break;
		case 7:	RME_Int_DebugPrint(State, "+EDI");	addr += State->DI.D;	break;
		}
		break;
	case 5:
		if( mmm & 8 )
		{
			// R/M == 5 and Mod == 0, disp32
			READ_INSTR32( addr );
			RME_Int_DebugPrint(State, "0x%x", addr);
		}
		else
		{
			RME_Int_DebugPrint(State, "EBP");
			addr = State->BP.D;
		}
		break;
	case 6:
		RME_Int_DebugPrint(State, "ESI");
		addr = State->SI.D;
		break;
	case 7:
		RME_Int_DebugPrint(State, "EDI");
		addr = State->DI.D;
		break;
	default:
		ERROR_S("Unknown mmm value passed to DoFunc32 (%i)", mmm);
		return RME_ERR_BUG;
	}
	if( !(mmm & 8) ) {
		RME_Int_DebugPrint(State, "+0x%x", disp);
		addr += disp;
	}
	RME_Int_DebugPrint(State, "]");
	*Segment = seg;
	*Offset = addr;
	return 0;
}

int RME_Int_GetMMM(tRME_State *State, const struct ModRM* modrm, uint16_t *Segment, uint32_t *Offset)
{
	uint16_t	ofs;
	 int	ret;
	
	if( State->Decoder.bOverrideAddress )
	{
		switch(modrm->mod)
		{
		case 0:	// No Offset
			ret = DoFunc32( State, modrm->mmm | 8, 0, Segment, Offset );
			if(ret)	return ret;
			break;
		case 1:	// disp8
			READ_INSTR8S( ofs );
			ret = DoFunc32( State, modrm->mmm, ofs, Segment, Offset);
			if(ret)	return ret;
			break;
		case 2:	// disp32
			READ_INSTR32( ofs );
			ret = DoFunc32( State, modrm->mmm, ofs, Segment, Offset );
			if(ret)	return ret;
			break;
		case 3:
			ERROR_S("mod=3 passed to RME_Int_GetMMM");
			return RME_ERR_BUG;
		default:
			ERROR_S("Unknown mod value passed to RME_Int_GetMMM (%i)", modrm->mod);
			return RME_ERR_BUG;
		}
	}
	else
	{
		switch(modrm->mod)
		{
		case 0:	// No Offset
			ret = DoFunc( State, modrm->mmm | 8, 0, Segment, Offset );
			if(ret)	return ret;
			break;
		case 1:	// 8 Bit
			READ_INSTR8S( ofs );
			ret = DoFunc( State, modrm->mmm, ofs, Segment, Offset);
			if(ret)	return ret;
			break;
		case 2:	// 16 Bit
			READ_INSTR16( ofs );
			ret = DoFunc( State, modrm->mmm, ofs, Segment, Offset );
			if(ret)	return ret;
			break;
		case 3:
			ERROR_S("mod=3 passed to RME_Int_GetMMM");
			return RME_ERR_BUG;
		default:
			ERROR_S("Unknown mod value passed to RME_Int_GetMMM (%i)", modrm->mod);
			return RME_ERR_BUG;
		}
	}
	return 0;
}

int RME_Int_DecodeModM(tRME_State *State, uint8_t **dst, const struct ModRM* modrm)
{
	int ret = 0;
	if( modrm->mod == 3 ) {
		*dst = RegB( State, modrm->mmm );
	}
	else {
		uint16_t	segment;
		uint32_t	offset;
		ret = RME_Int_GetMMM( State, modrm, &segment, &offset );
		if(ret)	return ret;
		ret = RME_Int_GetPtr(State, segment, offset, (void**)dst);
		if(ret)	return ret;
	}
	return ret;
}

int RME_Int_DecodeModMX(tRME_State *State, uint16_t **mem, const struct ModRM* modrm)
{
	int ret;
	if( modrm->mod == 3 ) {
		*mem = RegW( State, modrm->mmm );
	}
	else {
		uint16_t	segment;
		uint32_t	offset;
		ret = RME_Int_GetMMM( State, modrm, &segment, &offset );
		if(ret)	return ret;
		if( (segment * 0x10 + offset) % RME_BLOCK_SIZE == RME_BLOCK_SIZE-1 ) {
			ERROR_S("%x:%x Word read across boundary (0x%x)",
				State->CS, State->IP, segment * 0x10 + offset);
			return RME_ERR_BADMEM;
		}
		ret = RME_Int_GetPtr(State, segment, offset, (void**)mem);
		if(ret)	return ret;
	}
	return 0;
}

/**
 * \brief Parses the ModR/M byte as a 8-bit value
 * \param State	Emulator State
 * \param to	R field destination (ignored if NULL)
 * \param from	M field destination (ignored if NULL)
 */
int RME_Int_ParseModRM(tRME_State *State, uint8_t **reg, uint8_t **mem, int bReverse)
{
	 int	ret;

	struct ModRM modrm;
	ret = RME_Int_GetModRM(State, &modrm);
	if(ret) return ret;
	
	if(!bReverse && reg) *reg = RegB( State, modrm.rrr );
	if(mem) {
		ret = RME_Int_DecodeModM(State, mem, &modrm);
		if(ret) return ret;
	}
	if( bReverse && reg) *reg = RegB( State, modrm.rrr );
	return 0;
}

/**
 * \brief Parses the ModR/M byte as a 16-bit value
 * \param State	Emulator State
 * \param to	R field destination (ignored if NULL)
 * \param from	M field destination (ignored if NULL)
 */
int RME_Int_ParseModRMX(tRME_State *State, uint16_t **reg, uint16_t **mem, int bReverse)
{
	 int	ret;

	struct ModRM modrm;
	ret = RME_Int_GetModRM(State, &modrm);
	if(ret) return ret;
	
	if(!bReverse && reg) *reg = RegW( State, modrm.rrr );
	if(mem) {
		ret = RME_Int_DecodeModMX(State, mem, &modrm);
		if(ret) return ret;
	}
	if( bReverse && reg) *reg = RegW( State, modrm.rrr );
	return 0;
}
