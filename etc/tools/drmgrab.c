// drmgrab: dump what the display really scans out (the framebuffer of every active plane) as PPM files, to debug black screens.
// Build for the board: aarch64-linux-gnu-gcc drmgrab.c -I<sysroot>/usr/include/libdrm -ldrm -o drmgrab (needs root on the board).
// Usage: drmgrab /dev/dri/card0 /tmp/out   -> /tmp/out-plane<ID>.ppm, and one line per plane on stdout
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(int argc, char** argv)
{
    const char* dev = argc > 1 ? argv[1] : "/dev/dri/card0";
    const char* prefix = argc > 2 ? argv[2] : "/tmp/drmgrab";
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open"); return 1; }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmModePlaneRes* pr = drmModeGetPlaneResources(fd);
    if (!pr) { fprintf(stderr, "no plane resources\n"); return 1; }
    for (uint32_t i = 0; i < pr->count_planes; ++i) {
        drmModePlane* p = drmModeGetPlane(fd, pr->planes[i]);
        if (!p) continue;
        printf("plane %u: crtc=%u fb=%u\n", p->plane_id, p->crtc_id, p->fb_id);
        if (!p->fb_id) { drmModeFreePlane(p); continue; }
        drmModeFB2* fb = drmModeGetFB2(fd, p->fb_id);
        if (!fb) { printf("  GetFB2 failed\n"); drmModeFreePlane(p); continue; }
        printf("  %ux%u format=%.4s modifier=0x%llx pitch=%u handle=%u\n", fb->width, fb->height, (const char*)&fb->pixel_format,
               (unsigned long long)fb->modifier, fb->pitches[0], fb->handles[0]);
        int pfd = -1;
        if (fb->handles[0] && drmPrimeHandleToFD(fd, fb->handles[0], DRM_CLOEXEC | O_RDONLY, &pfd) == 0) {
            size_t size = (size_t)fb->pitches[0] * fb->height + fb->offsets[0];
            uint8_t* m = mmap(NULL, size, PROT_READ, MAP_SHARED, pfd, 0);
            if (m != MAP_FAILED) {
                char name[256]; snprintf(name, sizeof name, "%s-plane%u.ppm", prefix, p->plane_id);
                FILE* f = fopen(name, "wb");
                unsigned long nonblack = 0;
                if (f) fprintf(f, "P6\n%u %u\n255\n", fb->width, fb->height);
                for (uint32_t y = 0; y < fb->height; ++y) {
                    const uint8_t* row = m + fb->offsets[0] + (size_t)y * fb->pitches[0];
                    for (uint32_t x = 0; x < fb->width; ++x) {
                        uint8_t b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];   // XRGB8888/ARGB8888, little endian
                        if (r | g | b) ++nonblack;
                        if (f) { fputc(r, f); fputc(g, f); fputc(b, f); }
                    }
                }
                if (f) fclose(f);
                printf("  nonblack pixels: %lu of %lu -> %s\n", nonblack, (unsigned long)fb->width * fb->height, name);
                munmap(m, size);
            } else perror("  mmap");
            close(pfd);
        } else printf("  cannot export the buffer\n");
        drmModeFreeFB2(fb);
        drmModeFreePlane(p);
    }
    drmModeFreePlaneResources(pr);
    return 0;
}
