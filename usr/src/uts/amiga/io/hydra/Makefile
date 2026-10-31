CFLAGS	= -O -D_KERNEL -DSVR40 -DSVR4 -I../../.. -I../../inc $(KDB)

OBJ	= hydra.o

exp:	$(OBJ)
	ld -r -o exp $(OBJ)

clean:
	-rm -f $(OBJ) exp

hydra.o : hydra.h hydrauser.h ne2000.h ../kdb/kdebug.h
