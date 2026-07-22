#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "vdev.h"
#include "target_sil.h"
#include "vdev_prot_raspike.h"
#include "athrill_mpthread.h"
#include "devconfig.h"
#include "vdev_private.h"
#include "target_kernel_impl.h"
#include <pthread.h>
#include <unistd.h>
// For RasPike-ART
#include "raspike_protocol_api.h"

static MpthrIdType vdev_thrid;

static Std_ReturnType vdevProtRaspikeSilCb(unsigned int size, uintptr_t addr, const void* data);

static Std_ReturnType vdev_thread_do_init(MpthrIdType id);
static Std_ReturnType vdev_thread_do_proc(MpthrIdType id);

static MpthrOperationType vdev_op = {
	.do_init = vdev_thread_do_init,
	.do_proc = vdev_thread_do_proc,
};

int vdevProtRaspikeARTInit(const VdevIfComMethod *com)
{
  if (!com || !com->info) return STD_E_INVALID;
  RPComDescriptor *desc = (RPComDescriptor *)com->info;
  
  /* デバイスIOに書き込んだ際に呼ばれるコールバック関数 */
  SilSetWriteHook((const SilWriteHook)vdevProtRaspikeSilCb);

  int init_result = raspike_prot_init(desc);
  if (init_result < 0) {
    fprintf(stderr, "RASPIKE_FATAL,stage=protocol_init,error=%d\n", -init_result);
    return STD_E_INVALID;
  }
  
  mpthread_init();

  Std_ReturnType err = mpthread_register(&vdev_thrid, &vdev_op);
  if (err != STD_E_OK) return err;
  err = mpthread_start_proc(vdev_thrid);
  if (err != STD_E_OK) return err;
  return STD_E_OK;
}  


static void disable_interrupt(sigset_t *old)
{
  sigset_t sigset;
  sigemptyset(&sigset);

  sigaddset(&sigset,SIGUSR2);
  sigaddset(&sigset,SIGALRM);
  sigaddset(&sigset,SIGPOLL);
  sigprocmask(SIG_BLOCK, &sigset, old);
  return;
}

/* IOメモリへの書き込み */
Std_ReturnType vdevProtRaspikeSilCb(unsigned int size, uintptr_t addr, const void *data)
{
  (void)size;
  (void)data;
  if (addr != VDEV_TX_FLAG(0)) {
    // Called EV3 API. But RasPike-ART does not support it.
  
    printf("[WARN] RasPike-ART does not support EV3 API(addr = %p)\n", (void *)addr);

  }
  
  return STD_E_OK;

}

/* 受信スレッド */
static Std_ReturnType vdev_thread_do_init(MpthrIdType id)
{
  (void)id;
  /* 受信用スレッドでsignalを受けると、ON_STACKの判定が効かなくなるため、受信プロセスではsignalを受け取らないようにする*/
  disable_interrupt(NULL);
  return STD_E_OK;
}

static Std_ReturnType vdev_thread_do_proc(MpthrIdType id)
{
  (void)id;
  unsigned int fatal_errors = 0;
  while(1) {
    int result = raspike_prot_receive();
    if (result == -EPIPE || result == -EIO || result == -ENOTCONN) {
      if (++fatal_errors >= 3) {
        fprintf(stderr, "RASPIKE_FATAL,stage=receive,error=%d\n", -result);
        _exit(75); /* systemd restarts the runtime and re-negotiates a session */
      }
      struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
      nanosleep(&delay, NULL);
    } else {
      fatal_errors = 0;
    }
  }

  return STD_E_OK;

}
