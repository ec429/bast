#pragma name test
#pragma line start
#pragma renum =10 +5
.start
	let i=0
.loop
	print "Hello,\
 world!"
	let i=i+1
	if i<10 then goto %loop
.object
	~rlink test.obj
	print \10\02 ink 2\10\00; usr (@object+1)
