#ifndef _MMZ_VB_UAPI_H_
#define _MMZ_VB_UAPI_H_

#include <linux/types.h>

/*vb cfg flag*/
#define MMZ_VB_CFG_FLAG_INIT	(1 << 0)

/*vb pool flag*/
#define MMZ_VB_POOL_FLAG_DESTORY	(1 << 0)

/*set cfg cmd*/
typedef struct esVB_SET_CFG_REQ_S {
	enum esVB_UID_E uid;
	struct esVB_CONFIG_S cfg;
}VB_SET_CFG_REQ_S;

typedef struct esVB_SET_CFG_CMD_S {
	struct esVB_SET_CFG_REQ_S CfgReq;
}VB_SET_CFG_CMD_S;

/*get cfg cmd*/
typedef struct esVB_GET_CFG_REQ_S {
	enum esVB_UID_E uid;
}VB_Get_CFG_REQ_S;

typedef struct esVB_GET_CFG_RSP_S {
	struct esVB_CONFIG_S cfg;
}VB_Get_CFG_RSP_S;

typedef struct esVB_GET_CFG_CMD_S {
	struct esVB_GET_CFG_REQ_S req;
	struct esVB_GET_CFG_RSP_S rsp;
}VB_GET_CFG_CMD_S;

/*Init cfg cmd*/
typedef struct esVB_INIT_CFG_REQ_S {
	enum esVB_UID_E uid;
}VB_INIT_CFG_REQ_S;

typedef struct esVB_INIT_CFG_CMD_S {
	struct esVB_INIT_CFG_REQ_S req;
}VB_INIT_CFG_CMD_S;

/*UnInit cfg cmd*/
typedef struct esVB_UNINIT_CFG_REQ_S {
	enum esVB_UID_E uid;
}VB_UNINIT_CFG_REQ_S;

typedef struct esVB_UNINIT_CFG_CMD_S {
	struct esVB_UNINIT_CFG_REQ_S req;
}VB_UNINIT_CFG_CMD_S;

/*create pool cmd*/
typedef struct esVB_CREATE_POOL_REQ_S {
	struct esVB_POOL_CONFIG_S req;
}VB_CREATE_POOL_REQ_S;

typedef struct esVB_CREATE_POOL_RESP_S {
	__u32 PoolId;
} VB_CREATE_POOL_RESP_S;

typedef struct esVB_CREATE_POOL_CMD_S {
	struct esVB_CREATE_POOL_REQ_S PoolReq;
	struct esVB_CREATE_POOL_RESP_S PoolResp;
}VB_CREATE_POOL_CMD_S;

/*destory pool cmd*/
typedef struct esVB_DESTORY_POOL_REQ_S {
	__u32 PoolId;
}VB_DESTORY_POOL_REQ_S;

typedef struct esVB_DESTORY_POOL_RESP_S {
	__u32 Result;
}VB_DESTORY_POOL_RESP_S;

typedef struct esVB_DESTORY_POOL_CMD_S {
	struct esVB_DESTORY_POOL_REQ_S req;
	struct esVB_DESTORY_POOL_RESP_S rsp;
}VB_DESTORY_POOL_CMD_S;

typedef struct esVB_GET_BLOCK_REQ_S {
	enum esVB_UID_E uid;
	VB_POOL poolId;
	__u64 blkSize;
	char mmzName[ES_MAX_MMZ_NAME_LEN];
}VB_GET_BLOCK_REQ_S;
typedef struct esVB_GET_BLOCK_RESP_S {
	__u64 actualBlkSize;
	int fd;
	int nr; /*bitmap index in the pool*/
}VB_GET_BLOCK_RESP_S;
typedef struct esVB_GET_BLOCK_CMD_S
{
	struct esVB_GET_BLOCK_REQ_S getBlkReq;
	struct esVB_GET_BLOCK_RESP_S getBlkResp;
}VB_GET_BLOCK_CMD_S;

//corresponding to MMZ_VB_IOCTL_POOL_SIZE
typedef struct esVB_GET_POOLSIZE_CMD_S
{
	VB_POOL poolId;
	__u64 poolSize;
}VB_GET_POOLSIZE_CMD_S;

//corresponding to MMZ_VB_IOCTL_FLUSH_POOL
typedef struct esVB_FLUSH_POOL_CMD_S
{
	VB_POOL poolId;
	__u64 offset; // offset addr in the pool
	__u64 size; // size to be flushed
}VB_FLUSH_POOL_CMD_S;

//corresponding to MMZ_VB_IOCTL_BLOCK_TO_POOL
typedef struct esVB_BLOCK_TO_POOL_CMD_S
{
	int fd;	// Input: The dmabuf_fd of the block
	VB_POOL poolId; //Output: The pool which the block belongs to;
}VB_BLOCK_TO_POOL_CMD_S;

//corresponding to MMZ_VB_IOCTL_GET_BLOCK_OFFSET
typedef struct esVB_GET_BLOCKOFFSET_CMD_S
{
	int fd;	// Input: The dmabuf_fd, it might be the real block or the splittedBlock
	__u64 offset; // Output: The offset in pool
}VB_GET_BLOCKOFFSET_CMD_S;

//corresponding to MMZ_VB_IOCTL_SPLIT_DMABUF
typedef struct esVB_SPLIT_DMABUF_CMD_S {
	int fd; /* Input: The original dmabuf fd to be splitted */
	int slice_fd; /* Outpu: splitted dmabuf fd */
	__u64 offset; /* Input: offset of the buffer relative to the original dmabuf */
	__u64 len; /* size of the buffer to be splitted */
}VB_BLOCK_SPLIT_CMD_S;

//corresponding to MMZ_VB_IOCTL_DMABUF_REFCOUNT
typedef struct esVB_DMABUF_REFCOUNT_CMD_S
{
	int fd;	// Input: The dmabuf_fd
	__u64 refCnt; // Output: The file_count of the dmabuf
}VB_DMABUF_REFCOUNT_CMD_S;

//corresponding to MMZ_VB_IOCTL_RETRIEVE_MEM_NODE
typedef struct esVB_RETRIEVE_MEM_NODE_CMD_S
{
	int fd;	// Input: The dmabuf_fd
	void *cpu_vaddr; // Input: The virtual addr of cpu in user space
	int numa_node; // Ouput: return the NUMA node id of the memory
}VB_RETRIEVE_MEM_NODE_CMD_S;

//corresponding to MMZ_VB_IOCTL_DMABUF_SIZE
typedef struct esVB_DMABUF_SIZE_CMD_S
{
	int fd;	// Input: The dmabuf_fd
	__u64 size; // Output: The size of the dmabuf
}VB_DMABUF_SIZE_CMD_S;

#define MMZ_VB_IOC_MAGIC 		'M'
#define MMZ_VB_IOCTL_GET_BLOCK		_IOWR(MMZ_VB_IOC_MAGIC, 0x0, struct esVB_GET_BLOCK_CMD_S)
#define MMZ_VB_IOCTL_SET_CFG		_IOWR(MMZ_VB_IOC_MAGIC, 0x1, struct esVB_SET_CFG_CMD_S)
#define MMZ_VB_IOCTL_GET_CFG		_IOWR(MMZ_VB_IOC_MAGIC, 0x2, struct esVB_GET_CFG_CMD_S)
#define MMZ_VB_IOCTL_INIT_CFG		_IOWR(MMZ_VB_IOC_MAGIC, 0x3, struct esVB_INIT_CFG_CMD_S)
#define MMZ_VB_IOCTL_UNINIT_CFG		_IOWR(MMZ_VB_IOC_MAGIC, 0x4, struct esVB_UNINIT_CFG_CMD_S)
#define MMZ_VB_IOCTL_CREATE_POOL	_IOWR(MMZ_VB_IOC_MAGIC, 0x5, struct esVB_CREATE_POOL_CMD_S)
#define MMZ_VB_IOCTL_DESTORY_POOL	_IOWR(MMZ_VB_IOC_MAGIC, 0x6, struct esVB_DESTORY_POOL_CMD_S)
#define MMZ_VB_IOCTL_POOL_SIZE		_IOR(MMZ_VB_IOC_MAGIC, 0x7, struct esVB_GET_POOLSIZE_CMD_S)
#define MMZ_VB_IOCTL_FLUSH_POOL		_IOW(MMZ_VB_IOC_MAGIC, 0x8, struct esVB_FLUSH_POOL_CMD_S)
#define MMZ_VB_IOCTL_BLOCK_TO_POOL	_IOR(MMZ_VB_IOC_MAGIC, 0x9, struct esVB_BLOCK_TO_POOL_CMD_S)
#define MMZ_VB_IOCTL_GET_BLOCK_OFFSET	_IOR(MMZ_VB_IOC_MAGIC, 0xa, struct esVB_GET_BLOCKOFFSET_CMD_S)
#define MMZ_VB_IOCTL_SPLIT_DMABUF	_IOWR(MMZ_VB_IOC_MAGIC, 0xb, struct esVB_SPLIT_DMABUF_CMD_S)
#define MMZ_VB_IOCTL_DMABUF_REFCOUNT	_IOR(MMZ_VB_IOC_MAGIC, 0xc, struct esVB_DMABUF_REFCOUNT_CMD_S)
#define MMZ_VB_IOCTL_RETRIEVE_MEM_NODE	_IOR(MMZ_VB_IOC_MAGIC, 0xd, struct esVB_RETRIEVE_MEM_NODE_CMD_S)
#define MMZ_VB_IOCTL_DMABUF_SIZE	_IOR(MMZ_VB_IOC_MAGIC, 0xe, struct esVB_DMABUF_SIZE_CMD_S)

#endif
