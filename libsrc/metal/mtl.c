
/* jbm's interface to opencl devices */

/* This file contains the menu-callable functions, which in turn call
 * host functions which are typed and take an oap argument.
 * These host functions then call the gpu kernel functions...
 */

#include "quip_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif // HAVE_STRING_H

#ifdef HAVE_METAL

#define BUILD_FOR_METAL

#include "quip_prot.h"
#include "my_metal.h"	// 
#include "mtl_platform.h"	// 
#include "veclib_api.h"

#include "veclib/mtl_veclib_prot.h"

#include <Metal.h>		// apple only?
//#include <gl.h>			// apple only?
//#include "gl_info.h"
//#include "../opengl/glx_supp.h"

static const char *default_mtl_dev_name=NULL;
static const char *first_mtl_dev_name=NULL;
static int default_mtl_dev_found=0;


// Where does this comment go?

/* On the host 1L<<33 gets us bit 33 - but 1<<33 does not,
 * because, by default, ints are 32 bits.  We don't know
 * how nvcc treats 1L...  but we can try it...
 */

// make these C so we can link from other C files...

// We treat the device as a server, so "upload" transfers from host to device

static void mtl_mem_upload(QSP_ARG_DECL  void *dst, void *src, size_t siz, Platform_Device *pdp )
{
	cl_int status;

	// BUG need to check here
	//INSURE_ODP

	// copy the memory from host to device
	// CL_TRUE means blocking write/read

	if( curr_pdp == NULL ) return;

	status = clEnqueueWriteBuffer(OCLDEV_QUEUE(pdp),
			dst,
			CL_TRUE,	// blocking write
			0,		// offset
			siz,
			src,		// host mem address
			0,		// num events in wait list
			NULL,		// event wait list
			NULL);		// event - id for use if asynchronous
 
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clEnqueueWriteBuffer");
	}
}

static void mtl_mem_dnload(QSP_ARG_DECL  void *dst, void *src, size_t siz, Platform_Device *pdp )
{
	cl_int status;

	//INSURE_ODP

	if( curr_pdp == NULL ) return;

	//copy memory from device to host

//fprintf(stderr,"mtl_mem_dnload:  device = %s, src = 0x%lx, siz = %d, dst = 0x%lx\n",
//PFDEV_NAME(pdp),(long)src,siz,(long)dst);
	status = clEnqueueReadBuffer( OCLDEV_QUEUE(pdp),
			src,		// cl_mem
			CL_TRUE,	// blocking_read
			0,
			siz,
			dst,
			0,
			NULL,
			NULL);
 
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clEnqueueReadBuffer");
	}
}

void *TMPVEC_NAME(size_t size,size_t len,const char *whence)
{
	NWARN("mtl_tmpvec not implemented!?");
	return NULL;
}

void FREETMP_NAME(void *ptr,const char *whence)
{
	NWARN("mtl_freetmp not implemented!?");
}

#define MAX_OCL_DEVICES 5

static const char * available_mtl_device_name(QSP_ARG_DECL  const char *name,char *scratch_string)
{
	Platform_Device *pdp;
	const char *s;
	int n=1;

	s=name;
	// Why should we care how many devices there are?
	// Why have statically-allocated structures?
	while(n<=MAX_OCL_DEVICES){
		pdp = pfdev_of(QSP_ARG  s);
		if( pdp == NO_PFDEV ) return(s);

		// This name is in use
		n++;
		sprintf(scratch_string,"%s_%d",name,n);
		s=scratch_string;
	}
	sprintf(ERROR_STRING,"Number of %s OpenCL devices exceed configured maximum %d!?",
		name,MAX_OCL_DEVICES);
	WARN(ERROR_STRING);
	ERROR1(ERROR_STRING);
	return(NULL);	// NOTREACHED - quiet compiler
}

static void init_mtl_dev_memory(QSP_ARG_DECL  Platform_Device *pdp)
{
	char area_name[LLEN];
	Data_Area *ap;

	//strcpy(area_name,PFDEV_NAME(pdp));
	// BUG - make sure names will fit?
	sprintf(area_name,"%s.%s",
		PLATFORM_NAME(PFDEV_PLATFORM(pdp)),PFDEV_NAME(pdp));

	// what should the name for the memory area be???

	// address set to NULL says use custom allocator - see dobj/makedobj.c

	ap = pf_area_init(QSP_ARG  area_name,NULL,0, MAX_OCL_GLOBAL_OBJECTS,DA_OCL_GLOBAL,pdp);
	if( ap == NO_AREA ){
		sprintf(ERROR_STRING,
	"init_mtl_dev_memory:  error creating global data area %s",area_name);
		WARN(ERROR_STRING);
	}
	// g++ won't take this line!?
	SET_AREA_PFDEV(ap,pdp);

	// BUG should be per-device, not global table...
	pdp->pd_ap[PF_GLOBAL_AREA_INDEX] = ap;

	/* We used to declare a heap for constant memory here,
	 * but there wasn't much of a point because:
	 * Constant memory can't be allocated, rather it is declared
	 * in the .cu code, and placed by the compiler as it sees fit.
	 * To have objects use this, we would have to declare a heap and
	 * manage it ourselves...
	 * There's only 64k, so we should be sparing...
	 * We'll try this later...
	 */


	/* Make up another area for the host memory
	 * which is locked and mappable to the device.
	 * We don't allocate a pool here, but do it as needed...
	 */

	//strcat(cname,"_host");
	sprintf(area_name,"%s.%s_host",
		PLATFORM_NAME(PFDEV_PLATFORM(pdp)),PFDEV_NAME(pdp));

	ap = pf_area_init(QSP_ARG  area_name,(u_char *)NULL,0,MAX_OCL_MAPPED_OBJECTS,
							DA_OCL_HOST,pdp);
	if( ap == NO_AREA ){
		sprintf(ERROR_STRING,
	"init_mtl_dev_memory:  error creating host data area %s",area_name);
		ERROR1(ERROR_STRING);
	}
	SET_AREA_PFDEV(ap, pdp);
	pdp->pd_ap[PF_HOST_AREA_INDEX] = ap;

	/* Make up another psuedo-area for the mapped host memory;
	 * This is the same memory as above, but mapped to the device.
	 * In the current implementation, we create objects in the host
	 * area, and then automatically create an alias on the device side.
	 * There is a BUG in that by having this psuedo area in the data
	 * area name space, a user could select it as the data area and
	 * then try to create an object.  We will detect this in make_dobj,
	 * and complain.
	 */

	//strcpy(cname,dname);
	//strcat(cname,"_host_mapped");
	sprintf(area_name,"%s.%s_host_mapped",
		PLATFORM_NAME(PFDEV_PLATFORM(pdp)),PFDEV_NAME(pdp));

	ap = pf_area_init(QSP_ARG  area_name,(u_char *)NULL,0,MAX_OCL_MAPPED_OBJECTS,
						DA_OCL_HOST_MAPPED,pdp);
	if( ap == NO_AREA ){
		sprintf(ERROR_STRING,
	"init_mtl_dev_memory:  error creating host-mapped data area %s",area_name);
		ERROR1(ERROR_STRING);
	}
	SET_AREA_PFDEV(ap,pdp);
	pdp->pd_ap[PF_HOST_MAPPED_AREA_INDEX] = ap;

	if( verbose ){
		sprintf(ERROR_STRING,"init_mtl_dev_memory DONE");
		advise(ERROR_STRING);
	}
}

#define OCLDEV_EXTENSIONS(pdp)	OCLPF_EXTENSIONS(PFDEV_PLATFORM(pdp))
#define EXTENSIONS_PREFIX	"Extensions:  "

static void mtl_dev_info(QSP_ARG_DECL  Platform_Device *pdp)
{
	sprintf(MSG_STR,"%s:",PFDEV_NAME(pdp));
	prt_msg(MSG_STR);
	prt_msg("Sorry, no OpenCL-specific device info yet.");
}

static void mtl_info(QSP_ARG_DECL  Compute_Platform *cdp)
{
	int s;

	sprintf(MSG_STR,"Vendor:  %s",OCLPF_VENDOR(cdp));
	prt_msg(MSG_STR);
	sprintf(MSG_STR,"Version:  %s",OCLPF_VERSION(cdp));
	prt_msg(MSG_STR);
	sprintf(MSG_STR,"Profile:  %s",OCLPF_PROFILE(cdp));
	prt_msg(MSG_STR);

	// The extensions can be long...
	s = strlen(OCLPF_EXTENSIONS(cdp))+strlen(EXTENSIONS_PREFIX)+2;
	if( s > SB_SIZE(QS_SCRATCH(DEFAULT_QSP)) )
		enlarge_buffer( QS_SCRATCH(DEFAULT_QSP), s );
	sprintf(SB_BUF(QS_SCRATCH(DEFAULT_QSP)),"%s%s\n",EXTENSIONS_PREFIX,OCLPF_EXTENSIONS(cdp));
	prt_msg(SB_BUF(QS_SCRATCH(DEFAULT_QSP)));
}

static int extension_supported( Platform_Device *pdp, const char *ext_str )
{
	char *s;
	s = strstr( OCLDEV_EXTENSIONS(pdp), ext_str );
	return s==NULL ? 0 : 1;
}


#define MAX_PARAM_SIZE	128

static void init_mtl_device(QSP_ARG_DECL  cl_device_id dev_id,
							Compute_Platform *cpp)
{
	cl_int status;
	//long param_data[MAX_PARAM_SIZE/sizeof(long)];	// force alignment
	size_t psize;
	//char name[LLEN];
	char *name;
	char *extensions;
	char scratch[LLEN];
	const char *name_p;
	char *s;
	Platform_Device *pdp;
	CGLContextObj cgl_ctx=NULL;
	cl_context context;
	cl_command_queue command_queue; //"stream" in CUDA
	cl_context_properties props[3]={
		CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
		0,	// need to put cgl_ctx here
		0
		};

	// Find out space required
	status = clGetDeviceInfo(dev_id,CL_DEVICE_NAME,0,
		NULL,&psize);
	OCL_STATUS_CHECK(status,clGetDeviceInfo)
	name=getbuf(psize+1);
	status = clGetDeviceInfo(dev_id,CL_DEVICE_NAME,psize+1, name,&psize);
	OCL_STATUS_CHECK(status,clGetDeviceInfo)

	//strcpy(name,(char *)param_data);

	/* change spaces to underscores */
	s=name;
	while(*s){
		if( *s==' ' ) *s='_';
		s++;
	}

	/* We might have two of the same devices installed in a single system.
	 * In this case, we can't use the device name twice, because there will
	 * be a conflict.  The first one gets the name, then we have to check and
	 * make sure that the name is not in use already.  If it is, then we append
	 * a number to the string...
	 */
	name_p = available_mtl_device_name(QSP_ARG  name,scratch);	// use cname as scratch string
	pdp = new_pfdev(QSP_ARG  name_p);

	givbuf(name);

#ifdef CAUTIOUS
	if( pdp == NO_PFDEV ){
		sprintf(ERROR_STRING,"CAUTIOUS:  init_mtl_device:  Error creating cuda device struct for %s!?",name_p);
		WARN(ERROR_STRING);
		return;
	}
#endif /* CAUTIOUS */

	/* Remember this name in case the default is not found */
	if( first_mtl_dev_name == NULL )
		first_mtl_dev_name = PFDEV_NAME(pdp);

	/* Compare this name against the default name set in
	 * the environment, if it exists...
	 */
	if( default_mtl_dev_name != NULL && ! default_mtl_dev_found ){
		if( !strcmp(PFDEV_NAME(pdp),default_mtl_dev_name) )
			default_mtl_dev_found=1;
	}

	// allocate the memory for the platform-specific data
	PFDEV_ODI(pdp) = getbuf( sizeof( *PFDEV_ODI(pdp) ) );

	// Check for other properties...
	// find out how much space required...
	status = clGetDeviceInfo(dev_id,CL_DEVICE_EXTENSIONS,0,NULL,&psize);
	OCL_STATUS_CHECK(status,clGetDeviceInfo)
	extensions = getbuf(psize+1);
	status = clGetDeviceInfo(dev_id,CL_DEVICE_EXTENSIONS,psize+1,extensions,&psize);
	OCL_STATUS_CHECK(status,clGetDeviceInfo)
	{
		char *s;
		s=extensions;
		// change spaces to newlines for easier reading...
		while(*s){
			if( *s == ' ' ) *s = '\n';	
			s++;
		}
	}

	SET_OCLDEV_DEV_ID(pdp,dev_id);
	SET_PFDEV_PLATFORM(pdp,cpp);

	SET_PFDEV_MAX_DIMS(pdp,DEFAULT_PFDEV_MAX_DIMS);

	// On the new MacBook Pro, with two devices, the Iris_Pro
	// throws an error at clCreateCommandQueue *iff* we set
	// the share group property here...  Presumably because
	// that device doesn't handle the display?
	// We insert a hack below by excluding that device name,
	// but maybe there is another model where that would be
	// inappropriate?

	if( extension_supported(pdp,"cl_APPLE_gl_sharing") &&
			strcmp(PFDEV_NAME(pdp),"Iris_Pro")){

		CGLShareGroupObj share_group;
	
		cgl_ctx = CGLGetCurrentContext();
		if( cgl_ctx != NULL){
			// This means that we have an OpenGL window available...
			share_group = CGLGetShareGroup(cgl_ctx);
			if( share_group != NULL )
				props[1] = (cl_context_properties) share_group;
			else
				ERROR1("CAUTIOUS:  init_mtl_device:  CGL context found, but null share group!?");
		} else {
			advise("OpenCL initialized without an OpenGL context.");
		}
	}


	// Check for OpenGL capabilities
	//opengl_check(pdp);
#ifdef TAKEN_FROM_DEMO_PROG
#if (USE_GL_ATTACHMENTS)

    printf(SEPARATOR);
    printf("Using active OpenGL context...\n");

    CGLContextObj kCGLContext = CGLGetCurrentContext();              
    CGLShareGroupObj kCGLShareGroup = CGLGetShareGroup(kCGLContext);
    
    cl_context_properties properties[] = { 
        CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE, 
        (cl_context_properties)kCGLShareGroup, 0 
    };
        
    // Create a context from a CGL share group
    //
    ComputeContext = clCreateContext(properties, 0, 0, clLogMessagesToStdoutAPPLE, 0, 0);
	if(!ComputeContext)
		return -2;

#else	// ! USE_GL_ATTACHMENTS	

    // Connect to a compute device
    //
    err = clGetDeviceIDs(NULL, ComputeDeviceType, 1, &ComputeDeviceId, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to locate compute device!\n");
        return EXIT_FAILURE;
    }
  
    // Create a compute context 
    //
    ComputeContext = clCreateContext(0, 1, &ComputeDeviceId, clLogMessagesToStdoutAPPLE, NULL, &err);
    if (!ComputeContext)
    {
        printf("Error: Failed to create a compute context!\n");
        return EXIT_FAILURE;
    }
#endif	// ! USE_GL_ATTACHMENTS	
#endif // TAKEN_FROM_DEMO_PROG

	//create context on the specified device
//if( cgl_ctx != NULL )
//fprintf(stderr,"creating clContext with share properties for %s...\n",PFDEV_NAME(pdp));
	if( cgl_ctx == NULL ){
		context = clCreateContext(
			NULL,		// cl_context_properties *properties
			1,		// num_devices
			&dev_id,	// devices
			NULL,		// void *pfn_notify(const char *errinfo, const void *private_info, size_t cb, void *user_data )
			NULL,		// void *user_data
			&status		// cl_int *errcode_ret
		);
	} else {
		context = clCreateContext(
			props,		// cl_context_properties *properties
			0,		// num_devices
			NULL,	// devices
			clLogMessagesToStdoutAPPLE,	// void *pfn_notify(const char *errinfo, const void *private_info, size_t cb, void *user_data )
			NULL,		// void *user_data
			&status		// cl_int *errcode_ret
		);
	}
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clCreateContext");
		SET_OCLDEV_CTX(pdp,NULL);
		//return;
	}
	// BUG check return value for error

	SET_OCLDEV_CTX(pdp,context);

	//create the command_queue (stream)
//fprintf(stderr,"clContext = 0x%lx...\n",(long)context);
//fprintf(stderr,"init_mtl_device:  dev_id = 0x%lx\n",(long)dev_id);
	command_queue = clCreateCommandQueue(context, dev_id, 0, &status);
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clCreateCommandQueue");
		SET_OCLDEV_QUEUE(pdp,NULL);
		//return;
	} else {
		SET_OCLDEV_QUEUE(pdp,command_queue);
	}
	// set a ready flag?

	init_mtl_dev_memory(QSP_ARG  pdp);

	curr_pdp = pdp;
}

#define MAX_METAL_DEVICES	4

static void init_mtl_devices(QSP_ARG_DECL  Compute_Platform *cpp )
{
	cl_int	status;
	cl_uint		n_devs;
	int i;
	cl_device_id dev_tbl[MAX_METAL_DEVICES];

	if( cpp == NULL ) return;	// print warning?

	//get the device info
	status = clGetDeviceIDs( PF_OPD_ID(cpp), CL_DEVICE_TYPE_DEFAULT,
		MAX_METAL_DEVICES, dev_tbl, &n_devs);

	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clGetDeviceIDs");
		return;
		// BUG make sure to return cleanly...
	}
//fprintf(stderr,"init_mtl_devices:  %d device%s found\n",n_devs,n_devs==1?"":"s");

	if( verbose ){
		sprintf(ERROR_STRING,"%d OpenCL device%s found...",n_devs,
			n_devs==1?"":"s");
		advise(ERROR_STRING);
	}

	//default_mtl_dev_name = getenv(DEFAULT_OCL_DEV_VAR);
	/* may be null */

	for(i=0;i<n_devs;i++){
		/* how to name??? */

		/*
		char s[32];

		sprintf(s,"/dev/nvidia%d",i);
		check_file_access(QSP_ARG  s);
		*/

		init_mtl_device(QSP_ARG  dev_tbl[i],cpp);
	}

	//SET_PF_DISPATCH_FUNC( cpp, mtl_dispatch );
} // end init_mtl_devices

static int mtl_mem_alloc(QSP_ARG_DECL  Data_Obj *dp, dimension_t size, int align )
{
	cl_int status;

	// don't need current device, use object's...
	//INSURE_CURR_ODP(mtl_alloc_data);
//fprintf(stderr,"mtl_mem_alloc:  object %s device = %s\n",
//OBJ_NAME(dp),PFDEV_NAME(OBJ_PFDEV(dp)));

	// BUG what about alignment???

		OBJ_DATA_PTR(dp) = clCreateBuffer(OCLDEV_CTX(OBJ_PFDEV(dp)),
				CL_MEM_READ_WRITE, size, NULL, &status);

//fprintf(stderr,"mtl_mem_alloc:  object %s data ptr set to 0x%lx\n",
//OBJ_NAME(dp),(u_long)OBJ_DATA_PTR(dp));

	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status,"clCreateBuffer");
		sprintf(ERROR_STRING,"Attempting to allocate %d bytes.",size);
		advise(ERROR_STRING);
		return -1;
	}
	return 0;
}

static void mtl_mem_free(QSP_ARG_DECL  Data_Obj *dp)
{
	cl_int		ret;

	// BUG what about alignment???
//fprintf(stderr,"calling clReleaseMemObject %s 0x%lx\n",OBJ_NAME(dp),(u_long) OBJ_DATA_PTR(dp) );
	ret = clReleaseMemObject( (cl_mem) OBJ_DATA_PTR(dp) ); //free memory on device
	// BUG check return value
}

static void mtl_update_offset(QSP_ARG_DECL  Data_Obj *dp )
{
	ERROR1("mtl_update_offset not implemented!?");
}

#ifdef USE_METAL_SUBREGION
static cl_mem find_parent_buf(QSP_ARG_DECL  Data_Obj *dp, int *offset_p )
{
	int offset=0;

	while( ! OWNS_DATA(dp) ){
//fprintf(stderr,"%s does not own its data...\n",OBJ_NAME(dp));
		offset += OBJ_OFFSET(dp);	// Do we need to multiply?
//fprintf(stderr,"offset = %d\n",offset);
		dp = OBJ_PARENT(dp);
	}
//fprintf(stderr,"returning offset = %d\n",offset);
	*offset_p = offset;
//fprintf(stderr,"returning %s data ptr at 0x%lx\n",OBJ_NAME(dp),(u_long)OBJ_DATA_PTR(dp));
	return OBJ_DATA_PTR(dp);
}
#endif // USE_METAL_SUBREGION

/*
 * BUG - if we create a subregion for the offset area, then
 * things fail if we have multiple overlapping subregions!?
 * Better solution to keep the offset relative to the parent
 * buffer.
 */

static void mtl_offset_data(QSP_ARG_DECL  Data_Obj *dp, index_t offset)
{
#ifndef USE_METAL_SUBREGION
	/* The original code used subBuffers, but overlapping subregions
	 * don't work...
	 * So instead we use a common memory buffer, but keep track
	 * of the starting offset (in elements).  This offset has
	 * to be passed to the kernels.
	 */

//fprintf(stderr,"mtl_offset_data:  obj %s, offset = %d\n",OBJ_NAME(dp),offset);
//fprintf(stderr,"\tparent obj %s, parent offset = %d\n",OBJ_NAME(OBJ_PARENT(dp)),
//OBJ_OFFSET(OBJ_PARENT(dp)));

	if( IS_COMPLEX(dp) ){
#ifdef CAUTIOUS
		if( offset & 1 ){
			sprintf(ERROR_STRING,
	"CAUTIOUS:  mtl_offset_data:  odd element offset (%d) requested for complex object %s!?",
				offset,OBJ_NAME(dp));
			ERROR1(ERROR_STRING);
		}
#endif // CAUTIOUS
		offset /= 2;
//fprintf(stderr,"Adjusted offset (%d) for complex object %s\n",offset,OBJ_NAME(dp));
	} else if( IS_QUAT(dp) ){
#ifdef CAUTIOUS
		if( (offset & 3) != 0 ){
			sprintf(ERROR_STRING,
"CAUTIOUS:  mtl_offset_data:  element offset (%d) not a multiple of 4 for quaternion object %s!?",
				offset,OBJ_NAME(dp));
			ERROR1(ERROR_STRING);
		}
#endif // CAUTIOUS
		offset /= 4;
	}

	SET_OBJ_DATA_PTR(dp,OBJ_DATA_PTR(OBJ_PARENT(dp)));
	SET_OBJ_OFFSET( dp, OBJ_OFFSET(OBJ_PARENT(dp)) + offset );

#else // USE_METAL_SUBREGION
	cl_mem buf;
	cl_mem parent_buf;
	cl_buffer_region reg;
	cl_int status;
	int extra_offset;

	parent_buf = find_parent_buf(QSP_ARG  OBJ_PARENT(dp),&extra_offset);

#ifdef CAUTIOUS
	if( parent_buf == NULL )
		ERROR1("CAUTIOUS: mtl_offset_data:  no parent buffer!?");
#endif // CAUTIOUS

	reg.origin = (offset+extra_offset) * ELEMENT_SIZE(dp);

	// No - the region has to be big enough for all of the elements.
	// The safest thing is to include everything from the start
	// of the subregion to the end of the parent.  Note that this
	// cannot handle negative increments!?
	// reg.size = OBJ_N_MACH_ELTS(dp) * ELEMENT_SIZE(dp);

	//   p p p p p p p
	//   p p c c c p p
	//   p p p p p p p
	//   p p c c c p p

	reg.size =	  OBJ_SEQ_INC(dp)*(OBJ_SEQS(dp)-1)
			+ OBJ_FRM_INC(dp)*(OBJ_FRAMES(dp)-1)
			+ OBJ_ROW_INC(dp)*(OBJ_ROWS(dp)-1)
			+ OBJ_PXL_INC(dp)*(OBJ_COLS(dp)-1)
			+ OBJ_COMP_INC(dp)*(OBJ_COMPS(dp)-1)
			+ 1;
	reg.size *= ELEMENT_SIZE(dp);
//fprintf(stderr,"requesting subregion of %ld bytes at offset %ld\n",
//reg.size,reg.origin);

	buf = clCreateSubBuffer ( parent_buf,
				CL_MEM_READ_WRITE,
				CL_BUFFER_CREATE_TYPE_REGION,
		&reg,
			&status);
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clCreateSubBuffer");
		SET_OBJ_DATA_PTR(dp,OBJ_DATA_PTR(OBJ_PARENT(dp)));
	} else {
		SET_OBJ_DATA_PTR(dp,buf);
	}
	// BUG - Because this object doesn't "own" the data, the sub-buffer
	// won't be released when the object is destroyed, a possible memory
	// leak...
	// We need to add a special case, or make data releasing a
	// platform-specific function...
#endif // USE_METAL_SUBREGION
}

// use register_buf for interoperability with OpenGL...

static int mtl_register_buf(QSP_ARG_DECL  Data_Obj *dp)
{
#ifdef HAVE_OPENGL
	cl_mem img;
	cl_int status;

advise("mtl_register_buf calling clCreateFromGLTexture");

	// Texture2D deprecated on Apple
//fprintf(stderr,"obj %s has texture id %d\n",OBJ_NAME(dp),OBJ_TEX_ID(dp));
//fprintf(stderr,"obj %s has platform device %s\n",OBJ_NAME(dp),PFDEV_NAME(OBJ_PFDEV(dp)));

#ifdef FOOBAR
	img = clCreateFromGLTexture/*2D*/(//cl_context,
				OCLDEV_CTX( OBJ_PFDEV(dp) ),	// OCL context
				CL_MEM_READ_WRITE,
				GL_TEXTURE_2D,			// texture target, buffer must match
				0,				// mip level ?
				OBJ_TEX_ID(dp),			//gl_texture_id,
				&status);
#endif // FOOBAR
	img = clCreateFromGLBuffer(
				OCLDEV_CTX( OBJ_PFDEV(dp) ),	// OCL context
				CL_MEM_READ_WRITE,		// flags
				OBJ_TEX_ID(dp),			// from glBufferData?
				&status);

	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clCreateFromGLTexture");
		return -1;
	} else {
		SET_OBJ_DATA_PTR(dp,img);
	}

	// dp is a special buffer object...
	//cl_mem memobj;

	//cl_mem = clCreate
	return 0;
#else // ! HAVE_OPENGL
	WARN("mtl_register_buf:  Sorry, no OpenGL support in this build!?");
	return -1;
#endif // ! HAVE_OPENGL
}

// map_buf makes an opengl buffer object usable by OpenCL?
static int mtl_map_buf(QSP_ARG_DECL  Data_Obj *dp)
{
	cl_int status;

	glFlush();

//fprintf(stderr,"mtl_map_buf mapping %s\n",OBJ_NAME(dp));
	// Acquire ownership of GL texture for OpenCL Image
	status = clEnqueueAcquireGLObjects(//cl_cmd_queue,
			OCLDEV_QUEUE(OBJ_PFDEV(dp)),
			1,		// num_images
			(const cl_mem *)(& OBJ_DATA_PTR(dp)),
			0,
			0,
			0);

	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clEnqueueAcquireGLObjects");
		return -1;
	}

	// Now ready to execute kernel or other OpenCL operations ... ?
	return 0;
}

static int mtl_unmap_buf(QSP_ARG_DECL  Data_Obj *dp)
{
#ifdef HAVE_OPENGL
	cl_int status;

//fprintf(stderr,"mtl_unmap_buf un-mapping %s\n",OBJ_NAME(dp));
	// Release ownership of GL texture for OpenCL Image
	status = clEnqueueReleaseGLObjects(//cl_cmd_queue,
			OCLDEV_QUEUE(OBJ_PFDEV(dp)),
			1,	// num objects
			(const cl_mem *)(& OBJ_DATA_PTR(dp)),	// cl_mem *mem_objects
			0,		// num_events_in_wait_list
			NULL,		// const cl_event *wait_list
			NULL		// cl_event *event
			);
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clEnqueueReleaseGLObjects");
		return -1;
	}
	// Force pending CL commands to get executed
	status = clFlush( OCLDEV_QUEUE(OBJ_PFDEV(dp)) );
	if( status != CL_SUCCESS ){
		report_mtl_error(QSP_ARG  status, "clFlush");
	}
	
	// Bind GL texture and use for rendering
	glBindTexture( //gl_texture_target,
			GL_TEXTURE_2D,
			//gl_texture_id
			OBJ_TEX_ID(dp)

			);
	return 0;
#else // ! HAVE_OPENGL
	WARN("mtl_unmap_buf:  Sorry, no OpenGL support in this build!?");
	return -1;
#endif // ! HAVE_OPENGL
}

/* possible values for code:
 * CL_PLATFORM_PROFILE
 * CL_PLATFORM_VERSION
 * CL_PLATFORM_NAME
 * CL_PLATFORM_VENDOR
 * CL_PLATFORM_EXTENSIONS
 */

#define GET_PLATFORM_STRING(code)					\
	/* First figure out the required size */			\
	status = clGetPlatformInfo(platform_id,code,			\
		0,NULL,&ret_size);					\
	if( status != CL_SUCCESS ){					\
		report_mtl_error(QSP_ARG  status, "clGetPlatformInfo");	\
		return;							\
		/* BUG make sure to return cleanly... */		\
	}								\
	platform_str = getbuf(ret_size+1);				\
	status = clGetPlatformInfo(platform_id,code,			\
		ret_size+1,platform_str,&ret_size);			\
	if( status != CL_SUCCESS ){					\
		report_mtl_error(QSP_ARG  status, "clGetPlatformInfo");	\
		return;							\
		/* BUG make sure to return cleanly... */		\
	}

static void init_mtl_platform(QSP_ARG_DECL  cl_platform_id platform_id)
{
	Compute_Platform *cpp;
	cl_int status;
	//char param_data[MAX_PARAM_SIZE];
	char *platform_str;
	size_t ret_size;

	GET_PLATFORM_STRING(CL_PLATFORM_NAME)

	cpp = creat_platform(QSP_ARG  platform_str, PLATFORM_METAL);
	givbuf(platform_str);

	GET_PLATFORM_STRING(CL_PLATFORM_PROFILE)
	SET_OCLPF_PROFILE(cpp,platform_str);

	GET_PLATFORM_STRING(CL_PLATFORM_VERSION)
	SET_OCLPF_VERSION(cpp,platform_str);

	GET_PLATFORM_STRING(CL_PLATFORM_VENDOR)
	SET_OCLPF_VENDOR(cpp,platform_str);

	GET_PLATFORM_STRING(CL_PLATFORM_EXTENSIONS)
	SET_OCLPF_EXTENSIONS(cpp,platform_str);

	SET_PF_OPD_ID(cpp,platform_id);

	SET_PLATFORM_FUNCTIONS(cpp,mtl)

	SET_PF_FUNC_TBL(cpp,mtl_vfa_tbl);

	// BUG need to set vfa_tbl here too!

	//icp = create_item_context(QSP_ARG  pfdev_itp, PLATFORM_NAME(cpp) );
	//push_item_context(QSP_ARG  pfdev_itp, icp );
	push_pfdev_context(QSP_ARG  PF_CONTEXT(cpp) );
	init_mtl_devices(QSP_ARG  cpp);
	if( pop_pfdev_context(SINGLE_QSP_ARG) == NO_ITEM_CONTEXT )
		ERROR1("init_mtl_platform:  Failed to pop platform device context!?");
}

//In general Intel CPU and NV/AMD's GPU are in different platforms
//But in Mac OSX, all the OpenCL devices are in the platform "Apple"

#define MAX_CL_PLATFORMS	3

static int init_mtl_platforms(SINGLE_QSP_ARG_DECL)
{
	cl_platform_id	platform_ids[MAX_CL_PLATFORMS];
	cl_uint		num_platforms;
	cl_int		ret;
	int		i;

	// BUG need to add error checking on the return values...

	ret = clGetPlatformIDs(MAX_CL_PLATFORMS, platform_ids, &num_platforms);
//fprintf(stderr,"init_mtl_platform:  %d platform%s found\n",num_platforms,num_platforms==1?"":"s");

	for(i=0;i<num_platforms;i++)
		init_mtl_platform(QSP_ARG  platform_ids[i]);

	return num_platforms;
}

// this "platform" is OpenCL, not an OpenCL "platform" ...

void mtl_init_platform(SINGLE_QSP_ARG_DECL)
{
	init_mtl_platforms(SINGLE_QSP_ARG);

	check_mtl_vfa_tbl(SINGLE_QSP_ARG);
}

#endif /* HAVE_METAL */

