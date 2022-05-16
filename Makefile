CFLAGS = /nologo /DDLL_EXPORTS /c /DEBUG

build: so_stdio.dll

so_stdio.dll: so_stdio.obj
	link /nologo /dll /out:$@ /implib:so_stdio.lib $**

so_stdio.obj: so_stdio.c
	cl $(CFLAGS) /Fo$@ $**


clean : exe_clean obj_clean

obj_clean :
	del *.obj

exe_clean :
	del so_stdio.dll so_stdio.obj 