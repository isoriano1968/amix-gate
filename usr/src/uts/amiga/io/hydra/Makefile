CFLAGS	= -O -D_KERNEL -DSVR40 -DSVR4 -I../../.. -I../../inc -I../dp8390 $(KDB)

OBJ	= hydra.o

exp:	$(OBJ)
	ld -r -o exp $(OBJ)

clean:
	-rm -f $(OBJ) exp

hydra.o : hydrauser.h ../dp8390/dp8390.c ../dp8390/dp8390.h ../dp8390/dp8390reg.h
