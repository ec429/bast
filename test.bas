#pragma name test
#pragma line start
#pragma renum
.%start
	let i=0
	print "Hello,\
 world!"
	let i=i+1
	if i<10 then goto %start
