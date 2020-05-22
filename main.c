#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH_MAX 4096
#define HEIGHT_MAX 4096

#undef uint
// PGMでは各要素の値は16ビットで十分
typedef unsigned short uint;

typedef struct {
    char magic[3];
    size_t width;
    size_t height;
    uint max;
    uint image[HEIGHT_MAX][WIDTH_MAX];
} PNM;

// 最小値・最大値をまとめたもの
typedef struct {
    uint min;
    uint max;
} MinMax;

// ファイルからPGMイメージを読み出す
bool read_image(const char* filename, PNM* img) {
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        perror("read_image(fopen)");
        return false;
    }

    // ヘッダ読み出し
    int ret = fscanf(f, "%2s %zu %zu %hu", img->magic, &img->width, &img->height, &img->max);

    if (ret != 4) {
        fprintf(stderr, "read_image: cannot read the header\n");
        goto ERR;
    }

    if (img->width > WIDTH_MAX || img->height > HEIGHT_MAX) {
        fprintf(stderr, "read_image: image is too big\n");
        goto ERR;
    }

    if (img->magic[1] != '2') {
        fprintf(stderr, "read_image: image is not PGM(ASCII)\n");
        goto ERR;
    }

    // ピクセル読み出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            uint tmp;
            if (fscanf(f, "%hu", &tmp) != 1) {
                fprintf(stderr, "read_image: cannot read a pixel\n");
                goto ERR;
            }
            if (tmp > img->max) {
                fprintf(stderr, "read_image: pixel \"%hu\" (%zu %zu) exceeds the max \"%hu\"\n", tmp, i, j, img->max);
                goto ERR;
            }
            img->image[i][j] = tmp;
        }
    }

    fclose(f);
    return true;

ERR:
    fclose(f);
    return false;
}

// ファイルへPGMイメージを書き出す
bool write_image(const char* filename, const PNM* img) {
    FILE* f = fopen(filename, "w");
    if (f == NULL) {
        perror("write_image(fopen)");
        return false;
    }

    // ヘッダ書き出し
    fprintf(f, "%s\n%zu %zu\n%hu\n", img->magic, img->width, img->height, img->max);

    // ピクセル書き出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            fprintf(f, "%hu ", img->image[i][j]);
        }
        fprintf(f, "\n");
    }

    fclose(f);

    return true;
}

// 挿入ソート
// 参考: https://yaneurao.hatenadiary.com/entries/2009/11/26
void insertion_sort(uint *a, const size_t size){
    for (size_t i = 1; i < size; ++i) {
        uint buf = a[i];
        if (a[i-1] > buf) {
            size_t j = i;
            do {
                a[j] = a[j-1];
            } while (--j > 0 && a[j-1] > buf);
            a[j] = buf;
        }
    }
}

// メジアンフィルタ
void smooth_with_median(PNM* img) {
    // 画素配列は大きいのでヒープに置く
    // 二次元配列の確保は危険で面倒なのでPNM構造体で代用
    PNM* new_img = malloc(sizeof(PNM));

    // メジアンフィルタをかけて結果を新しい配列に入れる
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            uint a[] = {
                img->image[i-1][j-1],
                img->image[i-1][j],
                img->image[i-1][j+1],
                img->image[i][j-1],
                img->image[i][j],
                img->image[i][j+1],
                img->image[i+1][j-1],
                img->image[i+1][j],
                img->image[i+1][j+1]
            };

            insertion_sort(a, sizeof(a)/sizeof(a[0]));

            new_img->image[i][j] = a[4];
        }
    }

    // 結果を元の構造体に書き戻す
    for(size_t i = 1; i < img->height - 1; i++) {
        for(size_t j = 1; j < img->width - 1; j++) {
            img->image[i][j] = new_img->image[i][j];
        }
    }
}

// モザイク処理
void pixelize(PNM* img, size_t block_size) {
    for(size_t i = 0; i < img->height; i += block_size) {
        for(size_t j = 0; j < img->width; j += block_size) {
            // ブロック内の画素値の平均を求める
            unsigned long long avg = 0;
            size_t cnt = 0;
            for(size_t k = 0; k < block_size && i+k < img->height; k++) {
                for(size_t l = 0; l < block_size && j+l < img->width; l++) {
                    avg += img->image[i+k][j+l];
                    cnt++;
                }
            }
            avg /= cnt;

            // 求めた平均値でブロック全体を上書きする
            for(size_t k = 0; k < block_size && i+k < img->height; k++) {
                for(size_t l = 0; l < block_size && j+l < img->width; l++) {
                    img->image[i+k][j+l] = (uint)avg;
                }
            }
        }
    }
}

// 画素値の最小・最大を探す
MinMax find_min_max(const PNM* img) {
    MinMax mm;
    mm.min = img->max;
    mm.max = 0;

    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            const uint val = img->image[i][j];
            if (val < mm.min) mm.min = val;
            if (val > mm.max) mm.max = val;
        }
    }

    return mm;
}

// コントラストを補正する
void adjust_contrast(MinMax mm, PNM* img) {
    const uint diff = mm.max - mm.min;

    /*
    以下の場合は補正を行っても値が変わらないので無視する
        - 全ての画素値が同一のとき
        - 最小値が 0 、最大値が img->max のとき
    */
    if (diff == 0 || (mm.max == img->max && mm.min == 0)) {
        fprintf(stderr, "adjust_contrast: no operation performed\n");
        return;
    }

    // 補正を実行
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            uint* val = &(img->image[i][j]);
            *val = (img->max * (*val - mm.min)) / diff;
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "%s [input] [output] [block size]\n", argv[0]);
        return 0;
    }

    const unsigned long block_size = strtoul(argv[3], NULL, 10);
    if (block_size == ULONG_MAX) {
        perror("strtoul");
        return 1;
    }

    // 画素配列は大きいのでヒープに置く
    PNM* img = malloc(sizeof(PNM));

    if (!read_image(argv[1], img)) {
        fprintf(stderr, "main: error in reading image\n");
        return 1;
    }

    pixelize(img, block_size);

    if (!write_image(argv[2], img)) {
        fprintf(stderr, "main: error in writing image\n");
        return 1;
    }
}
