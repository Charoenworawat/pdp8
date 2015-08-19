/* pdp8.c
 *	:author Jetharin Kevin Charoenworawat *	:UTEID JKC2237
 *
 *	:intent Program that emulates the PDP-8
 *		by taking in instructions via
 *		an .obj file.
 *
 *
 *
 *	:slips 0 Slip Day Use
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Boolean Type Definition
typedef short Boolean;
#define TRUE 1
#define FALSE 0

//****************************************************************************//
//****************************************************************************//
// Constants
#define MAX_BYTES 4096
#define MAX_OPS 7 
#define OP_LENGTH 4 
int count = 0;
// Structs
//	None for PDP-8 Emulator

// PDP-8 Constants
short MASK_OPCODE = 0xE00;
short MASK_DI = 0x100;
short MASK_ZC = 0x080;
short MASK_ADDRESS = 0x07F;
short MASK_HIGH_FIVE = 0xF80;

short MASK_LINK = 0x1000; // bit 13
short MASK_ACCUMULATOR = 0x0FFF; // bit 0 - 11
short MASK_A_SIGN = 0x800; // bit 11
short MASK_A_VALUE = 0x7FF; // bit 0 - 10; excludes the sign bit

// Input/Output
//	function is ignored on this project
short MASK_DEVICE = 0x1F8;

// Operate Masks
//	Group 1 - listed in priority order
short MASK_CLA = 0x080;
short MASK_CLL = 0x040;
short MASK_CMA = 0x020;
short MASK_CML = 0x010;
short MASK_IAC = 0x001;
short MASK_RAR = 0x008;
short MASK_RAL = 0x004;
short MASK_ONETWO = 0x002;

//	Group 2 - listed in priority order
short MASK_SMA = 0x040;
short MASK_SZA = 0x020;
short MASK_SNL = 0x010;
short MASK_RSS = 0x008;
//    MASK_CLA = 0x080;
short MASK_OSR = 0x004;
short MASK_HLT = 0x002;
short MASK_BitZero = 0x001;

// PDP-8 Identifiers
short PDP8_Memory[MAX_BYTES];
short PDP8_PC;
short PDP8_Accumulator;

short entryPoint;
Boolean HLT;

// PDP-8 Skip Flags
Boolean PDP8_Skip_Flag;

// Output Identifiers
long long int PDP8_TIME;
char OPS_DONE[(MAX_OPS * OP_LENGTH) + 1];

Boolean verbose = FALSE;

// Local Identifiers
//	Used for procesing the .obj file
FILE* input;
char** inputArray;
int numLines;
//****************************************************************************//
//****************************************************************************//


//---------------------------------------------------------------------------//

//       Methods to display an error message and terminate the program       //

//---------------------------------------------------------------------------//

//	Case: no memory available after a malloc/realloc
void No_Memory()
{
	fprintf(stderr, "ERROR: Out of Memory... Exiting.\n");
	exit(1);
}

//	Case: invalid syntax for a particular line of input from the .obj file
void Invalid_Syntax(char* invalidLine)
{
	fprintf(stderr, "ERROR: Invalid Syntax for the line: %s... Exiting\n",
			invalidLine);
	exit(0);
}
//****************************************************************************//



//---------------------------------------------------------------------------//

//                              Helper Methods                               //

//---------------------------------------------------------------------------//

// Method that outputs to stderr the current information of the PDP-8
//	when the verbose mode is on
void verboseOutput()
{
	char* tempOpsDone = OPS_DONE;
	tempOpsDone++; // skips the leading space	

	fprintf(stderr, "Time %lld: PC=0x%03X instruction = 0x%03X (%s), rA = 0x%03X, rL = %d\n",
			PDP8_TIME, PDP8_PC, PDP8_Memory[PDP8_PC], tempOpsDone, 
			(PDP8_Accumulator & MASK_ACCUMULATOR), 
			((PDP8_Accumulator & MASK_LINK) >> 12));

	// reset OPS_DONE
	memset(OPS_DONE, 0, (MAX_OPS * OP_LENGTH) + 1);
}

// Method to get the effective address based on the instruction
short getEffectiveAddress(short directIndirect, short zeroCarry, short pageAddress)
{
	short effectiveAddress;

	short immediateAddress;
	// high order 5 bits -> current address high order 5 bits (current page)
	if(zeroCarry != 0)
	{
		immediateAddress = (PDP8_PC & MASK_HIGH_FIVE) + pageAddress;
	}
	// high order 5 bits -> zeroes 
	else
	{
		immediateAddress = pageAddress;
	}

	// the effective address will be fetched from the immediate address 
	if(directIndirect != 0)
	{
		// indirect memory referencing takes 1 cycle
		PDP8_TIME += 1;

		effectiveAddress = PDP8_Memory[immediateAddress];
	}
	// the effective address is the immediate address 
	else
	{
		effectiveAddress = immediateAddress;
	}
	
	return effectiveAddress;
}

// Method to convert a 16-bit short into a 12-bit version.
//	Warning: truncates the upper 4 bits
short Convert_To_12Bit(short sixteenBit)
{
	short twelveBit = (sixteenBit & MASK_A_VALUE);
	
	// mark the 12th bit sign based off of the sign for 16Bit
	//if((sixteenBit & 0x8000) != 0) 
	if(sixteenBit < 0)
	{
		// make sixteenBit a postive number for correct conversion to a 12 bit #
		twelveBit = ((~sixteenBit) + 1) & MASK_A_VALUE;

		// denote the twelveBit number as negative
		twelveBit = twelveBit & MASK_A_SIGN;
	}

	return twelveBit;
}	

// Method to convert a 12-bit short into a 16-bit version.
short Convert_To_16Bit(short twelveBit)
{
	short sixteenBit = (twelveBit & MASK_A_VALUE);
	
	if((twelveBit & MASK_A_SIGN) != 0)
	{
		sixteenBit = (~sixteenBit) + 1; // 2's comp negation
	}

	return sixteenBit;
}	
//****************************************************************************//



//---------------------------------------------------------------------------//

//     Methods to Get the Instructions for the PDP-8 Program from the .obj   //

//---------------------------------------------------------------------------//

// Method to read and store from the input file into a 2D array (inputArray)
void Get_Input()
{
	char c;
	int i = 0;
	
	int numLinesBufferSize = 20;
	inputArray = (char**)(malloc(numLinesBufferSize * sizeof(char*)));
	if(inputArray == NULL)
	{
		No_Memory();
	}
	numLines = 0;
	
	int bufferSize = 50;
	char* buffer = (char*)(malloc(bufferSize * sizeof(char)));
	if(buffer == NULL)
	{
		No_Memory();
	}

	// loop through each char of the input .obj file
	while((c = getc(input)) != EOF)
	{
		// Case: encountered a new line
		if(c == '\n')
		{
			numLines++;
			if(numLines == numLinesBufferSize - 1)
			{
				numLinesBufferSize = numLinesBufferSize * 2;
			
				inputArray = (char**)(realloc(inputArray, 
                                             (numLinesBufferSize * sizeof(char*))));
				if(inputArray == NULL)
				{
					No_Memory();
				}

			}

			// Store the buffered line into inputArray
			(* (inputArray + numLines-1)) = (char*)(malloc(bufferSize * sizeof(char)));
			if((* (inputArray + numLines-1)) == NULL)
			{
				No_Memory();
			}
			strcpy((* (inputArray + numLines-1)), buffer); 

			// Reset the buffer in order to eliminate overlap
			memset(buffer, 0, bufferSize);

			// Reset index for char placement into the buffer
			i = 0;
		}
		// General Case
		else
		{
			// Realloc the buffer if more space for a line is needed
			if(i == bufferSize - 1)
			{
				bufferSize = bufferSize * 2;
				
				buffer = (char*)(realloc(buffer, (bufferSize * sizeof(char))));
				if(buffer == NULL)
				{
					No_Memory();
				}
			}

			// Put the char, c, in the buffer
			buffer[i] = c;
			i++;
		}

	}

	free(buffer);
}
	
// Method to get the entry point for the PDP-8 program
//	from the input .obj file.  Method will process each
//	line until the EP is found.  If no EP is found, an
//	error message will be display and the program will
//	terminate.
int getEntryPoint()
{
	Boolean foundEP = FALSE;

	char* line;	
	char** tempInputArray = inputArray;
	int tempNumLines = numLines;

	char tempEP[10];
	int tempPC[1];
	int n; // # of items successfully read corresponding to the argument list in sscanf

	int EPLineIndex = 0;

	// Process the lines from the .obj file until the EP is found
	while(tempNumLines > 0 && !foundEP)
	{
		line = * tempInputArray;	

		n = sscanf(line, "%[^:]%*c %X", tempEP, tempPC);
		if(n == 2)
		{
			if(strcmp(tempEP, "EP") == 0)
			{
				foundEP = TRUE;
			}
			else // the string argument passed has invalid syntax
			{
				Invalid_Syntax(line);
			}

			entryPoint = * tempPC;
		}
		else
		{
			EPLineIndex++;

			tempInputArray++;
			tempNumLines--;
		}
	}

	// Check if an entry point was found
	if(foundEP == FALSE)
	{
		fprintf(stderr, "ERROR: No EP Found... Exiting\n");
		exit(0);
	}

	return EPLineIndex;
}

// Method to go process the contents of the input object file.
//	Incorrect syntax will terminate the program.
//	Instructions will be stored in PDP8_Memory.
void Fetch()
{
	// local variable to keep track of which line the EP was found on.
	int EPLineIndex = getEntryPoint();

	char* line;

	int tempPC[1];
	int tempInstruction[1];
	int n; // # of items successfully read corresponding to the argument list in sscanf()

	// Process the input from the .obj for instructions
	int lineIndex;
	for(lineIndex = 0; lineIndex < numLines; lineIndex++)
	{
		line = * inputArray;	

		if(lineIndex != EPLineIndex)
		{
			n = sscanf(line, "%X %*c %X", tempPC, tempInstruction);
			if(n != 2)
			{
				Invalid_Syntax(line);
			}
	
			PDP8_Memory[* tempPC] = * tempInstruction; 		
		}

		inputArray++;
		free(line);
	}
	
	//free(inputArray); // invalid pointer??
}
//****************************************************************************//



//---------------------------------------------------------------------------//

//                      Methods to Run the PDP-8 Program                     //

//---------------------------------------------------------------------------//
void Decode_Execute();
void Run_PDP8()
{
	PDP8_PC = entryPoint;
	PDP8_Accumulator = 0;
	PDP8_TIME = 0;
	memset(OPS_DONE, 0, (MAX_OPS * OP_LENGTH) + 1);

	PDP8_Skip_Flag = FALSE;
	HLT = FALSE;

	while(!HLT)
	{
		Decode_Execute();
	}
}

void Input_Output(short instruction);
void Operate_Group_One(short instruction);
int Operate_Group_Two(short instruction);
// Method that interprets the current instruction as
//	determined by the program counter. The instruction
//	will then be carried out if possible; if not,
//	an additional method is invoked.
void Decode_Execute()
{/*
	// Escape from recursion
	if(HLT)
	{
		return;
	}
*/
	// identifier used when adjusting the PC
	Boolean jump = FALSE;

	// section for local identifiers to be set up for
	//	the current instruction
	short currentInstruction = PDP8_Memory[PDP8_PC];
	short opcode = (currentInstruction & MASK_OPCODE) >> 9;

	short di = currentInstruction & MASK_DI;
	short zc = currentInstruction & MASK_ZC;
	short address = (currentInstruction & MASK_ADDRESS);

	short effectiveAddress;
	Boolean memReference = FALSE;

	// Case: opcode is set to IOT
	if(opcode == 6) 
	{
		PDP8_TIME += 1;

		Input_Output(currentInstruction);
	}
	// Case: opcode is set to operate
	else if(opcode == 7) 
	{
		PDP8_TIME += 1;

		short bit8 = currentInstruction & MASK_DI; // same bit as the D/I
 		if(bit8 == 0) // Group 1
		{
			Operate_Group_One(currentInstruction);
		}
		else // Group 2
		{
			HLT = Operate_Group_Two(currentInstruction);
		}
	}		
	// General Case: the remaining opcodes that reference memory
	else
	{
		PDP8_TIME += 2;

		
		effectiveAddress = getEffectiveAddress(di, zc, address);
		memReference = TRUE;

		if(opcode == 0) // AND
		{
			PDP8_Accumulator = PDP8_Memory[effectiveAddress] & 
                                           PDP8_Accumulator;
					
			strcat(OPS_DONE, " AND");
		}
		else if(opcode == 1) // TAD
		{	
			PDP8_Accumulator = PDP8_Memory[effectiveAddress] + PDP8_Accumulator;

			strcat(OPS_DONE, " TAD");
		}
		else if(opcode == 2) // ISZ
		{
			PDP8_Memory[effectiveAddress] += 1;

			if((PDP8_Memory[effectiveAddress] & 0xFFF) == 0)
			{
				PDP8_Skip_Flag = TRUE;
			}
				
			strcat(OPS_DONE, " ISZ");
		}
		else if(opcode == 3) // DCA
		{	
			PDP8_Memory[effectiveAddress] = (PDP8_Accumulator & MASK_ACCUMULATOR);

			// Clear the accumulator while maintaining the link bit
			PDP8_Accumulator = PDP8_Accumulator & MASK_LINK;

			strcat(OPS_DONE, " DCA");
		}
		else if(opcode == 4) // JMS
		{
			// store the next immediate instruction in the effective address
			PDP8_Memory[effectiveAddress] = PDP8_PC + 1;
		
			// increment the effective address to where the PC
			//	will be set to to carry out the subroutine
			effectiveAddress += 1;

			strcat(OPS_DONE, " JMS");
			jump = TRUE;
		}
		else if(opcode == 5) // JMP
		{
			// dec the cycles as a jump only takes 1 cycle
			PDP8_TIME--;

			strcat(OPS_DONE, " JMP");
			jump = TRUE;
		}
	}

	// add an I if an indirect memory reference was made 
	if(memReference && (di != 0))
	{
		strcat(OPS_DONE, " I");
	}

	// Output to stderr
	if(verbose)
	{
		verboseOutput();
	}
	else
	{
		// still have to reset the memory for OPS_DONE
		memset(OPS_DONE, 0, (MAX_OPS * OP_LENGTH) + 1);
	}

	// Update PC
	if(jump)
	{
		PDP8_PC = effectiveAddress;
	}
	else
	{
		if(PDP8_Skip_Flag == TRUE)
		{
			PDP8_PC += 2;

			PDP8_Skip_Flag = FALSE;
		}
		else
		{		
			PDP8_PC += 1;
		}
	}

	// Recursive Call to carry out the next instruction
	// Decode_Execute();
}

void Input_Output(short instruction)
{
	// get at bits 3 - 8 then shift right three for value normalization
	short device = (instruction & MASK_DEVICE) >> 3;

	if(device == 3)
	{
		char c = getchar();		
		
		if(c == EOF)
		{
			strcat(OPS_DONE, " HLT");
		}
		else
		{
			PDP8_Accumulator = (PDP8_Accumulator & MASK_LINK) + c;

			strcat(OPS_DONE, " IOT 3");
		}
	}
	else if(device == 4)
	{ 
		putchar(PDP8_Accumulator & 0x0FF); 

		strcat(OPS_DONE, " IOT 4");
	}
	else
	{
		HLT = TRUE; 

		strcat(OPS_DONE, " HLT");
	}
}

void Operate_Group_One(short instruction)
{
	if((instruction & MASK_CLA) != 0)
	{
		PDP8_Accumulator = (PDP8_Accumulator & MASK_LINK);

		strcat(OPS_DONE, " CLA");
	}	
	if((instruction & MASK_CLL) != 0)
	{
		PDP8_Accumulator = PDP8_Accumulator & MASK_ACCUMULATOR;

		strcat(OPS_DONE, " CLL");
	}
	if((instruction & MASK_CMA) != 0)	
	{
		short originalLink = PDP8_Accumulator & MASK_LINK;
		short originalAccumulator = PDP8_Accumulator & MASK_ACCUMULATOR;

		PDP8_Accumulator = originalLink + ((~originalAccumulator) & MASK_ACCUMULATOR);
		
		strcat(OPS_DONE, " CMA");
	}
	if((instruction & MASK_CML) != 0)	
	{
		short originalLink = PDP8_Accumulator & MASK_LINK;
		short originalAccumulator = PDP8_Accumulator & MASK_ACCUMULATOR;

		PDP8_Accumulator = ((~originalLink) & MASK_LINK) + originalAccumulator;

		strcat(OPS_DONE, " CML");
	}	
	if((instruction & MASK_IAC) != 0)
	{
		PDP8_Accumulator++;

		strcat(OPS_DONE, " IAC");
	}
	
	// Rotate Section // 
	//	The Link Bit is involved in the rotates
	int numShifts = 1;
	if((instruction & MASK_ONETWO) != 0)
	{
		numShifts++;
	}
	
	// Error Case: Both the rotate right and left bits are active
	if( ((instruction & MASK_RAR) !=0) && ((instruction & MASK_RAL) != 0) )
	{
		strcat(OPS_DONE, " NOP");
	}
	else if((instruction & MASK_RAR) != 0) // RAR; if numShifts == 2: RTR
	{
		short tempLinkAccumulator = (PDP8_Accumulator >> numShifts) | 
                                    (PDP8_Accumulator << (13-numShifts));

		// ensures the upper 3 bits of the short are 0
		PDP8_Accumulator = (tempLinkAccumulator & MASK_LINK) + 
                           (tempLinkAccumulator & MASK_ACCUMULATOR);

		if(numShifts == 1)	
		{
			strcat(OPS_DONE, " RAR");
		}
		else
		{
			strcat(OPS_DONE, " RTR");
		}
	}
	else if((instruction & MASK_RAL) != 0) // RAL; if numShifts == 2: RTL
	{
		short tempLinkAccumulator = (PDP8_Accumulator << numShifts) | 
                                    (PDP8_Accumulator >> (13-numShifts));

		// ensures the upper 3 bits of the short are 0
		PDP8_Accumulator = (tempLinkAccumulator & MASK_LINK) + 
                           (tempLinkAccumulator & MASK_ACCUMULATOR);
		
		if(numShifts == 1)	
		{
			strcat(OPS_DONE, " RAL");
		}
		else
		{
			strcat(OPS_DONE, " RTL");
		}
	}	
}

int Operate_Group_Two(short instruction)
{
	// Conditional Section
	if( ((instruction&MASK_SMA) == 0) && ((instruction&MASK_SZA) == 0) && 
		((instruction&MASK_SNL) == 0))
	{
		// clear the Skip flag	
		PDP8_Skip_Flag = FALSE;
	}

	if((instruction & MASK_SMA) != 0) 
	{
		if((PDP8_Accumulator & MASK_A_SIGN) != 0)
		{
			PDP8_Skip_Flag = TRUE;	
		}

		strcat(OPS_DONE, " SMA");
	}
	if((instruction & MASK_SZA) != 0)
	{
		if((PDP8_Accumulator & MASK_ACCUMULATOR) == 0)
		{
			PDP8_Skip_Flag = TRUE;	
		}

		strcat(OPS_DONE, " SZA");
	}
	if((instruction & MASK_SNL) != 0)
	{
		if((PDP8_Accumulator & MASK_LINK) != 0)
		{
			PDP8_Skip_Flag = TRUE;	
		}
	
		strcat(OPS_DONE, " SNL");
	}

	if((instruction & MASK_RSS) != 0) // RSS
	{
		// complement the Skip flag
		//PDP8_Skip_Flag = ~PDP8_Skip_Flag; 
		if(PDP8_Skip_Flag == TRUE)
		{
			PDP8_Skip_Flag = FALSE;
		}
		else
		{
			PDP8_Skip_Flag = TRUE;
		}

		strcat(OPS_DONE, " RSS");
	}
	if((instruction & MASK_CLA) != 0) // CLA
	{
		PDP8_Accumulator = (PDP8_Accumulator & MASK_LINK);

		strcat(OPS_DONE, " CLA");
	}
	if((instruction & MASK_OSR) != 0) // OSR --> NOP
	{
		strcat(OPS_DONE, " NOP");
	}
	if((instruction & MASK_HLT) != 0) // HLT
	{
		strcat(OPS_DONE, " HLT");
	}
	if((instruction & MASK_BitZero) != 0) // bit for EAE instructions --> NOP
	{
		strcat(OPS_DONE, " NOP");
	}

	return ((instruction & MASK_HLT) != 0); // returns if there is a HLT
}
//****************************************************************************//



//---------------------------------------------------------------------------//

//                       Program's Driver Method                             //

//---------------------------------------------------------------------------//

int main(int argc, char** argv)
{
	// Skips the Program's Name as an Argument
	argc--;
	argv++;

	// Case: too many arguments
	if(argc > 2)
	{
		fprintf(stderr, "ERROR: Invalid number of arguments passed... Exiting.\n");
		exit(1);
	}
	// Case: no object file passed
	else if(argc == 0)
	{
		fprintf(stderr, "ERROR: No PDP-8 object file passed... Exiting.\n");
		exit(0);
	}
	else if(argc == 2)
	{
		char verboseArgument[3] = "-v";

		if(strcmp(*argv, verboseArgument) != 0)
		{
			fprintf(stderr, "ERROR: Invalid option %s\n", *argv);
			exit(0);
		}

		verbose = TRUE;
		
		argc--;
		argv++;
	}

	input = fopen(* argv, "r");
	if(input == NULL)
	{
		fprintf(stderr, "Can't open %s\n", * argv);
		exit(0);
	}

	Get_Input();
	Fetch();		
	fclose(input);

	Run_PDP8();

	exit(0); // Successful Termination
}
