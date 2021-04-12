CC	= gcc
CFLAGS	= -Og -g -Wall

lancxasm: lancxasm.o expression.o pseudo.o m6502.o symbols.o

lancxasm.o: lancxasm.h lancxasm.c

expression.o: lancxasm.h expression.c

pseudo.o: lancxasm.h pseudo.c

m6502.o: lancxasm.h m6502.c

symbols.o: lancxasm.h symbols.c
