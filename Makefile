CC	= gcc
CFLAGS	= -O2 -g -Wall

lancxasm: dstring.o lancxasm.o expression.o pseudo.o m6502.o symbols.o

lancxasm.o: lancxasm.h dstring.h charclass.h lancxasm.c

expression.o: lancxasm.h dstring.h expression.c

pseudo.o: lancxasm.h dstring.h charclass.h pseudo.c

m6502.o: lancxasm.h dstring.h charclass.h m6502.c

symbols.o: lancxasm.h dstring.h symbols.c
