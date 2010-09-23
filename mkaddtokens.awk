# Generate addtokens.c from tokens (the output of x-tok)
BEGIN { FS="\t" }
/[[:upper:]<=>$ ]+\t0x[[:xdigit:]]/ { printf "addtokd(\"%s\", %s);\n", $1, $2; next }
