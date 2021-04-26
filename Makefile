CC	= gcc
CFLAGS	= -O2 -g -Wall

laxasm: dstring.o laxasm.o expression.o pseudo.o m6502.o symbols.o

laxasm.o: laxasm.h dstring.h charclass.h laxasm.c

expression.o: laxasm.h dstring.h expression.c

pseudo.o: laxasm.h dstring.h charclass.h pseudo.c

m6502.o: laxasm.h dstring.h m6502.c

symbols.o: laxasm.h dstring.h symbols.c
