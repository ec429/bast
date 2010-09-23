bast - ZX Basic (and Z80 assembler) text to tape

--------------------------------------

SYNOPSIS
bast {[-b] <basfile> | -l <linkobj> | -a <asmfile> | -I <incpath> | -I0 | -L <linkpath> | -L0 | -W[-] <warning> | <other options>}* {-o <outobj> | -oi | -t <outtap>}

OPTIONS CONTROLLING SOURCE FILES
[-b] <basfile>		any argument not beginning with a dash which is not preceded by a specifier (such as -l, -I, -t etc.), or any argument preceded by the specifier -b, is treated as a BASIC file to be read.  If more than one such file appears, they will be appear on the tape in the order of their appearance
-a <asmfile>		specifies an assembly file to be assembled and compiled in (equivalently one could use BASIC files containing only #asm/#endasm blocks)
-l <linkobj>		specifies an object (ie. m/c) file to be compiled in eg. when producing TAP output
-I <incpath>		adds an entry to the include path (used for both #include and #merge directives)
-I0					clears the include path (including default entries)
-L <linkpath>		adds an entry to the linking path (used for -l and #link when producing eg. TAP output)
-L0					clears the linking path (including default entries)

OPTIONS CONTROLLING WARNINGS
-W all				Enables all warnings
-W- all				Disables all warnings (generally -W- <warning> disables whatever -W <warning> enables)

OPTIONS CONTROLLING THE TYPE OF OUTPUT
-o <outobj>			Strips out all BASIC, leaving only any machine code from -a, -l, #asm, or #link (but NOT #rasm or #rlink as these appear in the BASIC), and writes the result into an object file
-oi					As -o but write each BINARY segment into its own individual object file, named as '<name>.obj' where <name> is the segment's Name.  Don't write the linked files, only the assembled ones
-t <outtap>			Creates a .TAP file of all the segments in order (first, files named on the command line, in order of appearance; then, segments resulting from directives, in the order in which those directives appeared.  #include does not create new segments; only #link and #asm do that)

--------------------------------------

SOURCE FILE DIRECTIVES (BAS)
#pragma <pr> [<args>]	#pragma directives must precede all other directives and all source lines; only blank lines may precede a #pragma
#pragma name <name>		If eg. TAP output is produced, <name> will be used as the Name of the segment resulting from this file
#pragma line <line>		If eg. TAP output is produced, this file will be stored as though saved with 'SAVE "<name>" LINE <line>' (i.e., <line> is the autorun)
#pragma renum			This file is not numbered (only labelled) and should be auto-numbered.  #pragma renum and line-numbers may not be mixed in a single source file: if you are going to auto-number, don't hardcode numbers in eg. GOTOs as these will NOT be updated
#include <incfile>		Includes the contents of <incfile> (another Basic file, found by searching the include path) at the location of the #include
#link <linkobj>			If eg. TAP output is produced, compile in <linkobj> (a machine code object file, found by searching the link path)
#asm		#endasm		Delimits a block of Z80 assembler, which will become a BINARY segment as though it had been linked.  The #asm block may contain its own directives which will be treated as though the #asm block had appeared in its own file (eg. it may have #pragmas at the start)
<num> #rlink			As #link but compiles into a BASIC REM statement instead of a BINARY segment.  The code linked should be relocatable.  <num> is the linenumber (technically #rlink is a statement).  If the binary has a Name, it is ignored
<num> #rasm				As #asm but compiles into a BASIC REM statement instead of a BINARY segment.  The code within should be relocatable.  <num> is the linenumber (technically #rasm is a statement).  If the binary has a Name (eg. from #pragma name), it is ignored

OTHER SOURCE FILE NON-BASIC ENTITIES
.<label>				A label.  <label> must match "[[:alpha:]][[:alnum:]_]*"; that is, it must start with a letter (either case) and consist of letters, underscores and numbers only.  Labels must occur at the start of line; that is, they may not be preceded by whitespace.  They should be followed by a newline
	%<label>			Within an expression, is replaced by the line number of label <label>, which need not be in the same source file
	HEX <hex>			Within an expression, is replaced by the decimal value of hexadecimal <hex> (ie. like BIN).  E.g. "HEX 1FF" -> "511"
	0x<hex>				As HEX <hex>
	OCT <oct>			Within an expression, is replaced by the decimal value of octal <oct>.  E.g. "OCT 307" -> "199"
	0<oct>				As OCT <oct>

--------------------------------------

COMPILATION PROCESS
Step 0: Director.  Directives are parsed and where possible acted upon (eg #include files are included).  Any #[r]asm/#endasm blocks are separated out for the assembler.  The assembler is fork()ed and sets to work on producing the object code (assuming there is any work for it to do)
Step 1: Tokeniser.  Each line of BASIC is split into a series of tokens (such as KEYWORDS (characters 0xA3 to 0xFF) and numbers (in Sinclair floating point notation: 0x0E + 4mantissa + 1exponent, or 0E 00 {00|FF}sign LSB MSB 00 for small integers)).  Renumbering is performed if directed
Step 2: Linker.  If there were any #rlink or #rasm sections, wait until they are assembled (#rasm only) and insert them into REM statements in the BASIC segment; if there were any -l, #link or #asm sections, wait until they are assembled (#asm only) and then add them (as BINARY segments) to the compilation.  They will appear in the virtual tape in the order in which they were encountered (first the -ls, then the #s in the order they appear in the BASIC source files).  If a segment has no name, one will be generated for it of the form basN or binN where N starts from 0.  If a segment's name clashes with a preexisting segment it will replace the old segment.  It is during this step that labels are translated
Step 3: Output.  Produce whichever kind of output is required (objects, tape, etc)

Unnecessary steps will be skipped; eg. if output type is Objects (-o <file> or -oi), Step 1 will not be performed

--------------------------------------

FORMAT OF OBJECT FILES (.obj)
Optional ORG directive of the form '@B1FF' where B1FF is some hex value (big endian!).  If the code is to be linked it must have an ORG; if it is #rlinked it should not have, and any ORG it does have will be ignored
Optional NAME directive of the form '#<name>'
Optional symbol table consisting of rows of the form '&label == FF B1' where FF B1 is the (little endian) address of the label
Rows of eight bytes followed by '==' and a checksum byte, all in hex pairs, like '01 00 00 c9 FD CB 01 81 == 7E'.  The checksum byte is the XOR of all eight data bytes
Any two consecutive bytes may be replaced with a label in the form '%label $$', which will be converted by the linker to the address of that label (in little endian form); the $$ is a placeholder for the MSB.  Certain special labels are provided by the compiler: '%%org' points to the ORG for this object file (for #rlinked files, to the computed ORG), '%%bas<file>:M:N' points to the start of statement N of line M (useful eg. for pointing to m/c in REM statements); '<file>:M:N' may in turn be replaced with '%blabel' where blabel is the label of some BASIC line.  There are also "indexed labels", that is, '%label+4F' or '%label-22' or whatever, where the offset must be a hex pair.  Checksums are calculated on the assumption that all labels evaluate to 00 00
