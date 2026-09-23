/* ptpktscan.c -- count Intel PT packet types in an AUX file already on disk.
 *
 * pt_capture2 prints the same summary at the end of a capture (`OVF=... PSB=... PTW=...'),
 * but a capture taken with `--no-decode' writes the AUX and no summary.  This tool gives
 * the packet-level evidence for such a file after the fact -- in particular the number of
 * on-chip OVF (buffer overflow) packets, which IS trace loss and which no drain statistic
 * can show: a capture can have lost = 0 / truncated = 0 (the reader kept up) and still be
 * full of OVFs (the packet generator could not keep up).
 *
 *   ptpktscan FILE.aux [--ovf-gaps OUT]
 *
 * --ovf-gaps writes "tsc_before gap_tsc" per OVF (the control flow lost to it, in TSC
 * cycles: the gap between the last TSC before the OVF and the first one after it).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <intel-pt.h>

int main(int argc, char **argv)
{
    const char *path = NULL, *gaps = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ovf-gaps") && i + 1 < argc) gaps = argv[++i];
        else if (argv[i][0] != '-') path = argv[i];
        else { fprintf(stderr, "usage: ptpktscan FILE.aux [--ovf-gaps OUT]\n"); return 2; }
    }
    if (!path) { fprintf(stderr, "usage: ptpktscan FILE.aux [--ovf-gaps OUT]\n"); return 2; }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return 1; }
    struct stat st;
    if (fstat(fd, &st) || st.st_size <= 0) { perror("fstat"); return 1; }
    void *base = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 1; }

    struct pt_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.size = sizeof(cfg);
    cfg.begin = (uint8_t *)base;
    cfg.end = (uint8_t *)base + st.st_size;
    struct pt_packet_decoder *dec = pt_pkt_alloc_decoder(&cfg);
    if (!dec) { fprintf(stderr, "pt_pkt_alloc_decoder failed\n"); return 2; }

    uint64_t n_ovf = 0, n_tsc = 0, n_mtc = 0, n_cyc = 0, n_psb = 0, n_psbend = 0,
             n_ptw = 0, n_tip = 0, n_tnt = 0, n_pge = 0, n_pgd = 0, n_total = 0, n_resync = 0;
    uint64_t first_tsc = 0, last_tsc = 0, total_lost = 0;
    uint64_t *pend = NULL; size_t np = 0, pcap = 0;
    FILE *gf = gaps ? fopen(gaps, "w") : NULL;

    int err = pt_pkt_sync_forward(dec);
    while (err >= 0) {
        struct pt_packet pkt;
        int e = pt_pkt_next(dec, &pkt, sizeof(pkt));
        if (e < 0) { n_resync++; err = pt_pkt_sync_forward(dec); continue; }
        n_total++;
        switch (pkt.type) {
        case ppt_ovf:
            n_ovf++;
            if (np == pcap) { pcap = pcap ? pcap * 2 : 1024; pend = realloc(pend, pcap * sizeof(*pend)); }
            pend[np++] = last_tsc;
            break;
        case ppt_tsc: {
            uint64_t t = pkt.payload.tsc.tsc;
            for (size_t j = 0; j < np; j++) {
                uint64_t g = t > pend[j] ? t - pend[j] : 0;
                total_lost += g;
                if (gf) fprintf(gf, "%llu %llu\n", (unsigned long long)pend[j], (unsigned long long)g);
            }
            np = 0; last_tsc = t; if (!first_tsc) first_tsc = t;
            n_tsc++; break; }
        case ppt_mtc: n_mtc++; break;
        case ppt_cyc: n_cyc++; break;
        case ppt_psb: n_psb++; break;
        case ppt_psbend: n_psbend++; break;
        case ppt_ptw: n_ptw++; break;
        case ppt_tip: n_tip++; break;
        case ppt_tip_pge: n_pge++; break;
        case ppt_tip_pgd: n_pgd++; break;
        case ppt_tnt_8: case ppt_tnt_64: n_tnt++; break;
        default: break;
        }
    }
    pt_pkt_free_decoder(dec);
    if (gf) fclose(gf);

    double span_tsc = (last_tsc > first_tsc) ? (double)(last_tsc - first_tsc) : 0.0;
    printf("file=%s bytes=%lld\n", path, (long long)st.st_size);
    printf("OVF=%llu PSB=%llu PSBEND=%llu PTW=%llu TSC=%llu MTC=%llu CYC=%llu "
           "TIP=%llu PGE=%llu PGD=%llu TNT=%llu total_packets=%llu pkt_resyncs=%llu\n",
           (unsigned long long)n_ovf, (unsigned long long)n_psb, (unsigned long long)n_psbend,
           (unsigned long long)n_ptw, (unsigned long long)n_tsc, (unsigned long long)n_mtc,
           (unsigned long long)n_cyc, (unsigned long long)n_tip, (unsigned long long)n_pge,
           (unsigned long long)n_pgd, (unsigned long long)n_tnt,
           (unsigned long long)n_total, (unsigned long long)n_resync);
    printf("tsc_span=%.0f lost_cycles_in_ovf=%llu (%.4f %% of the traced span)\n",
           span_tsc, (unsigned long long)total_lost,
           span_tsc > 0 ? 100.0 * (double)total_lost / span_tsc : 0.0);
    printf("tsc_first=%llu tsc_last=%llu\n", (unsigned long long)first_tsc,
           (unsigned long long)last_tsc);
    return 0;
}
