#pragma name test
#pragma line start
#pragma renum =5 +5
	rem This test program demonstrates several features of bast, such as:
	rem * line splicing (with \ at EOL)
	rem * \10\01inline colour codes\10\00 and other nonprinting characters (with \xx)
	rem * object code insertion into REM statements (and labels that evaluate to pointers to the bytes)
	rem * automatic line-numbering
.start
	let i=0
.loop
	print i+1;".";tab 4;"Hello,\
 world!"
	let i=i+1
	if i<10 then goto %loop
.object
	~rlink test.obj
	print \10\02 ink 2\10\00; usr (@object+1)
