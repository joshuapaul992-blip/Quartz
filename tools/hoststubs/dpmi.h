#ifndef STUB_DPMI_H
#define STUB_DPMI_H
typedef union {
  struct { unsigned long edi,esi,ebp,cflag,ebx,edx,ecx,eax; } d;
  struct { unsigned short ax,ax_hi,bx,bx_hi,cx,cx_hi,dx,dx_hi,
                          si,si_hi,di,di_hi,bp,bp_hi,
                          flags,es,ds,fs,gs,ip,cs,sp,ss; } x;
  struct { unsigned char al,ah,bl,bh,cl,ch,dl,dh; } h;
} __dpmi_regs;
typedef struct { unsigned long handle,address,size; } __dpmi_meminfo;
int  __dpmi_int(int, __dpmi_regs*);
int  __dpmi_physical_address_mapping(__dpmi_meminfo*);
int  __dpmi_free_physical_address_mapping(__dpmi_meminfo*);
int  __dpmi_allocate_ldt_descriptors(int);
int  __dpmi_free_ldt_descriptor(int);
int  __dpmi_set_segment_base_address(int, unsigned long);
int  __dpmi_set_segment_limit(int, unsigned long);
int  __dpmi_yield(void);
#endif
