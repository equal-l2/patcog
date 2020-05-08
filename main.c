#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH_MAX 4096
#define HEIGHT_MAX 4096

#undef uint
typedef uint32_t uint;

typedef struct {
    char magic[3];
    size_t width;
    size_t height;
    uint max;
    uint val[HEIGHT_MAX][WIDTH_MAX];
} Image;

Image* read_image(const char* filename) {
    // open file
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        perror("fopen");
        return NULL;
    }

    // allocate memory
    Image* img = calloc(1, sizeof(Image));

    if (img == NULL) {
        perror("malloc");
        goto ERROR;
    }

    // read header
    int n = fscanf(f, "%2s %zu %zu", img->magic, &img->width, &img->height);

    if (n != 3) {
        fprintf(stderr, "Corrupted header");
        goto ERROR;
    }

    img->magic[2] = '\0';

    char ver = img->magic[1];
    if (img->magic[0] != 'P' || '1' > ver || ver > '3') {
        fprintf(stderr, "Unknown version \"%s\"", img->magic);
        goto ERROR;
    }

    // set max
    switch (ver) {
        case '1': img->max = 1; break;
        default : fscanf(f, "%" PRIu32 "u", &img->max); break;
    }

    for(size_t i = 0; i < img->height; i++) {
        // with ppm, each pixel has 3 values
        size_t w = ver == '3' ? img->width*3 : img->width;

        for(size_t j = 0; j < w; j++) {
            uint tmp;
            fscanf(f, "%" PRIu32 "u", &tmp);
            if (tmp > img->max) {
                fprintf(stderr, "Warning: value \"%" PRIu32 "\"exceeds the max \"%" PRIu32 "\", wrapping", tmp, img->max);
                tmp = img->max;
            }
            img->val[i][j] = tmp;
        }
    }

    return img;

ERROR:
    free(img);
    return NULL;
}

void write_image(const char* filename, const Image* img) {
    FILE* f = fopen(filename, "w");
    fprintf(f, "%s\n%zu %zu\n", img->magic, img->width, img->height);

    switch (img->magic[1]) {
        case '1': /* no-op */ break;
        default : fprintf(f, "%" PRIu32 "\n", img->max); break;
    }

    for(size_t i = 0; i < img->height; i++) {
        // with ppm, each pixel has 3 values
        size_t w = img->magic[1] == '3' ? img->width*3 : img->width;

        for(size_t j = 0; j < w; j++) {
            fprintf(f, "%" PRIu32 " ", img->val[i][j]);
        }
        fprintf(f, "\n");
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "%s [input] [output]", argv[0]);
        return 0;
    }

    Image* img = read_image(argv[1]);
    write_image(argv[2], img);
}
