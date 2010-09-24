#pragma name test
#pragma line start
#pragma renum =5 +5
	rem \08\08\08\08\08\08\08\08\08\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\
\11\03 \11\07This test program demonstrates  several features of \11\05bast\11\07, e.g.:\
\11\02*\11\07 line splicing (with \ at EOL) \
\11\02*\11\07 \11\01inline colour codes\11\07 and other nonprinting characters (use \xx)\
\11\02*\11\07 object code insertion into \11\06REM\11\07statements (and labels that     evaluate to pointers to the m/c)\
\11\02*\11\07 indexed labels (useful for    \
\11\06REM\11\07med object code)             \
\11\02*\11\07 automatic line-numbering      \
\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f\8f
.start
	load "testbin" code 32768,4
	let i=0
.loop
	print i+1;".";tab 4;"Hello,\
 world!"
	let i=i+1
	if i<10 then goto %loop
.object
	~link test.obj
	print \10\02 ink 2\10\00; usr usr @object+01
