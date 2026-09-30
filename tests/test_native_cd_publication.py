"""Exercise the native scheduler when a decoder releases a queued sector."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_pending_sector_publishes_without_another_command(tmp_path):
    source = (ROOT / 'pc_port/native_boot.c').read_text()
    start = source.index('static int advance_cd_devices(')
    end = source.index('\nstatic int epoch_advance_devices', start)
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef struct { uint32_t chcr; uint64_t due; unsigned waiting_for_request; } MusashiCdDma3State;
typedef struct {
 uint64_t cycle, due, sector_publish_due, sector_due;
 unsigned phase, incoming_valid, interrupt, response_count;
 unsigned announced_valid, announced_claimed, command;
} MusashiCdOwnedState;
typedef struct { void *cd_drive, *cd_dma, *spu; struct { struct { int busy; } cd_frame; } irq; uint64_t cd_irq_production_limit; } NativeBoot;
static MusashiCdOwnedState drive;
static unsigned publications, fetches;
static int dma_busy, dma_waiting;
#define MUSASHI_SPU_CD_AUDIO_QUEUE_LIMIT 8192u
typedef struct { unsigned pending_frames; } MusashiSpuCdAudioSnapshot;
static unsigned pcm_pending;
static int musashi_spu_cd_audio_snapshot(void *p,MusashiSpuCdAudioSnapshot *s) {
 (void)p;s->pending_frames=pcm_pending;return 1;
}
static int musashi_cd_dma3_get_state(void *p, MusashiCdDma3State *s) { (void)p; *s=(MusashiCdDma3State){.chcr=dma_busy?0x01000000u:0,.due=2000001,.waiting_for_request=dma_waiting}; return 1; }
static int musashi_cd_owned_get_state(void *p, MusashiCdOwnedState *s) { (void)p; *s=drive; return 1; }
static int musashi_cd_dma3_advance(void *p, uint64_t c) { (void)p;(void)c;return 1; }
static int musashi_cd_owned_advance_without_fetch(void *p, uint64_t c) {
 (void)p; assert(c>=drive.cycle); drive.cycle=c;
 if(drive.incoming_valid && c>=drive.sector_publish_due && !drive.interrupt && !drive.response_count && (!drive.announced_valid || drive.announced_claimed || drive.command==0x1b)) {
  drive.incoming_valid=0; drive.announced_claimed=0; drive.interrupt=1; ++publications;
 }
 return 1;
}
static uint64_t musashi_cd_owned_bounded_cycle(const MusashiCdOwnedState *s,uint64_t c) { (void)s;return c; }
static int musashi_cd_owned_advance(void *p,uint64_t c) { ++fetches;return musashi_cd_owned_advance_without_fetch(p,c); }
'''
    harness += source[start:end]
    harness += r'''
int main(void) {
 NativeBoot boot={.cd_drive=(void*)1,.cd_dma=(void*)2};
 /* During decoding, INT1 for the next sector is acknowledged and its FIFO
    claimed. Another sector waits at a future publication retry deadline. */
 drive=(MusashiCdOwnedState){.cycle=100,.sector_publish_due=101,
  .sector_due=102,.incoming_valid=1,.announced_valid=1,.announced_claimed=1};
 assert(advance_cd_devices(&boot,1000));
 assert(publications==1 && !drive.incoming_valid && drive.interrupt==1);
 assert(fetches==0); /* Publication must not fetch over an occupied buffer. */
 drive=(MusashiCdOwnedState){.cycle=100,.sector_publish_due=101,
  .sector_due=102,.incoming_valid=1,.announced_valid=1,.announced_claimed=0};
 assert(advance_cd_devices(&boot,1000));
 assert(publications==1 && drive.incoming_valid && drive.cycle==100);
 drive.announced_claimed=1; drive.interrupt=1;
 assert(advance_cd_devices(&boot,1000));
 assert(publications==1 && drive.incoming_valid);
 drive.interrupt=0; drive.response_count=1;
 assert(advance_cd_devices(&boot,1000));
 assert(publications==1 && drive.incoming_valid);
 /* ReadS may discard an acknowledged notification, never its pending IRQ. */
 drive.response_count=0;drive.command=0x1b;
 assert(advance_cd_devices(&boot,1000));
 assert(publications==2 && !drive.incoming_valid);
 /* Slow host work during an IRQ must not turn into a second sector period.
    The source instruction contributes two guest cycles in this fixture. */
 drive=(MusashiCdOwnedState){.cycle=100,.command=0x1b};
 boot.irq.cd_frame.busy=1;boot.cd_irq_production_limit=102;
 assert(advance_cd_devices(&boot,1000000));assert(drive.cycle==102);
 boot.cd_irq_production_limit=104;
 assert(advance_cd_devices(&boot,1000000));assert(drive.cycle==104);
 /* An active chopped transfer protects its FIFO through slow host cuts. */
 dma_busy=1;
 assert(advance_cd_devices(&boot,2000000));assert(drive.cycle==104);
 /* Paused DMA must not prevent a CD command from making progress. */
 dma_waiting=1;boot.cd_irq_production_limit=106;
 assert(advance_cd_devices(&boot,2000000));assert(drive.cycle==106);
 dma_busy=0;dma_waiting=0;
 boot.spu=(void*)3;pcm_pending=6000;
 assert(advance_cd_devices(&boot,2000000));assert(drive.cycle==106);
 pcm_pending=0;
 boot.irq.cd_frame.busy=0;
 assert(advance_cd_devices(&boot,1000000));assert(drive.cycle==1000000);
 drive=(MusashiCdOwnedState){.cycle=100,.command=0x1b,.sector_due=200};
 assert(advance_cd_devices(&boot,1000000));assert(drive.cycle==200);
 return 0;
}
'''
    path = tmp_path / 'probe.c'
    path.write_text(harness)
    binary = tmp_path / 'probe'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
