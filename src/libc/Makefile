NAME	:=	libc
SNAME	:=	$(NAME).a
DNAME	:=	$(NAME).so
SRC		:=	$(wildcard src/*.c)
ASM_SRC	:=	$(wildcard src/*.s)
OBJ		:=	$(SRC:.c=.o)
OBJ		+=	$(ASM_SRC:.s=.o)
CFLAGS	:=	-std=c99 -ffreestanding -Wextra -pedantic -Wall -fPIC -nostdinc -nostdlib 
ASFLAGS :=	$(CFLAGS) -c
LDFLAGS	:=	
CC		:=	gcc
RANLIB	:=	ranlib
AS		:=	gcc

.PHONY:	all clean

all: $(SNAME) $(DNAME)

$(SNAME):	$(OBJ)
	$(AR) rc $@ $^
	$(RANLIB) $@

$(DNAME):	$(OBJ)
	$(CC) -shared -Wl,-e,_dlstart -Wl,-Bsymbolic-functions $(LDFLAGS) $^ -o $@

clean:
	$(RM) $(OBJ) $(SNAME) $(DNAME)
