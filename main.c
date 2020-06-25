#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH_MAX 4096
#define HEIGHT_MAX 4096
#define QUEUE_SIZE 65536

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

    // 画素読み出し
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

    // 画素書き出し
    for(size_t i = 0; i < img->height; i++) {
        for(size_t j = 0; j < img->width; j++) {
            fprintf(f, "%3hu ", img->image[i][j]);
        }
        fprintf(f, "\n");
    }

    fclose(f);

    return true;
}

// 文字列から double 型の数を取り出す
bool get_double(const char* str, double* ret) {
    char* c = NULL;
    const double d = strtod(str, &c);
    if (str == c) {
        fprintf(stderr, "get_double: input is not a valid double\n");
        return false;
    }
    if (d == HUGE_VAL || d == -HUGE_VAL) {
        fprintf(stderr, "get_double: input is out of range\n");
        return false;
    }
    *ret = d;
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

    free(new_img);
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

// 最小値・最大値をまとめたもの
typedef struct {
    uint min;
    uint max;
} MinMax;

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

// スケール処理
bool scale(PNM* img, double height_factor, double width_factor) {
    // スケール後の画像の大きさは、係数を乗じて四捨五入する
    const double new_height = round(height_factor * img->height);
    const double new_width = round(width_factor * img->width);

    // スケール前後の画像の大きさを出力
    fprintf(stderr, "scale: %zdx%zd -> %.0fx%.0f\n", img->height, img->width, new_height, new_width);

    // スケール後に大きすぎたり0になったりする場合は中止
    if (new_height > (double)HEIGHT_MAX || new_width > (double)WIDTH_MAX) {
        fprintf(stderr, "read_image: cannot scale, resulting image will be too big\n");
        return false;
    }
    if (new_height == 0 || new_width == 0) {
        fprintf(stderr, "read_image: cannot scale, resulting image will be zero-sized\n");
        return false;
    }

    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = (size_t)new_height;
    new_img->width = (size_t)new_width;
    new_img->max = img->max;

    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            double tmp;

            // 補間原点：スケール後画像の対象画素を、スケール前画像空間に戻した際の実数座標の整数部

            const double h_dist = modf(i/height_factor, &tmp); // 補間原点からの高さ方向の距離
            const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

            const double w_dist = modf(j/width_factor, &tmp); // 補間原点からの幅方向の距離
            const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

            if (h_base == img->height-1 || w_base == img->width-1) {
                // 補間原点が画像の端であるとき
                // 補間できないので補間原点の画素値でとりあえず埋めておく
                new_img->image[i][j] = img->image[h_base][w_base];
            } else {
                new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                );
            }
        }
    }

    *img = *new_img;

    free(new_img);

    return true;
}

static inline double deg_to_rad(double deg) {
    // pi/180;
    const static double factor = 0.01745329251994329576923690768488612713442;
    return deg * factor;
}

// (x0, y0) を中心に角度 theta だけ回転
// theta は radian
bool rotate(PNM* img, double theta, double x0, double y0) {
    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = img->height;
    new_img->width = img->width;
    new_img->max = img->max;

    const double sint = sin(theta);
    const double cost = cos(theta);
    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            // 逆変換で元の座標を算出する
            const double x_orig = cost*(j-x0)+sint*(i-y0)+x0;
            const double y_orig = -sint*(j-x0)+cost*(i-y0)+y0;

            if (
                0 <= x_orig && x_orig <= (new_img->width - 1) &&
                0 <= y_orig && y_orig <= (new_img->height - 1)
            ) {
                /* 補間処理 */
                double tmp;
                const double h_dist = modf(y_orig, &tmp); // 補間原点からの高さ方向の距離
                const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

                const double w_dist = modf(x_orig, &tmp); // 補間原点からの幅方向の距離
                const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

                if (h_base == img->height-1 || w_base == img->width-1) {
                    // 補間原点が画像の端であるとき
                    // 補間できないので0を入れておく
                    new_img->image[i][j] = 0;
                } else {
                    new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                    );
                }
            } else {
                // 元の点は存在しないので0を入れておく
                new_img->image[i][j] = 0;
            }
        }
    }

    *img = *new_img;
    free(new_img);

    return true;
}

/*
 Affine transformation: (x0, y0) -> (X, Y)
 / \   /   \ /  \   / \
 |X| = |a b| |x0| + |c|
 |Y|   |d e| |y0|   |f|
 \ /   \   / \  /   \ /
 */
typedef struct {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
} AffineArgs;

// アフィン変換
bool affine_trans(PNM* img, AffineArgs args) {
    // 変換行列の行列式
    const double det = args.a*args.e-args.b*args.d;

    if (det == 0) {
        fprintf(stderr, "affine_trans: determinant is zero\n");
        return false;
    }

    PNM* new_img = malloc(sizeof(PNM));
    strcpy(new_img->magic, img->magic);
    new_img->height = img->height;
    new_img->width = img->width;
    new_img->max = img->max;

    for(size_t i = 0; i < new_img->height; i++) {
        for(size_t j = 0; j < new_img->width; j++) {
            // 逆変換で元の座標を算出する
            const double x_orig = (args.e*(j-args.c)-args.b*(i-args.f))/det;
            const double y_orig = (-args.d*(j-args.c)+args.a*(i-args.f))/det;

            if (
                0 <= x_orig && x_orig <= (new_img->width - 1) &&
                0 <= y_orig && y_orig <= (new_img->height - 1)
            ) {
                /* 補間処理 */
                double tmp;
                const double h_dist = modf(y_orig, &tmp); // 補間原点からの高さ方向の距離
                const size_t h_base = (size_t)tmp; // 補間原点の高さ方向座標

                const double w_dist = modf(x_orig, &tmp); // 補間原点からの幅方向の距離
                const size_t w_base = (size_t)tmp; // 補間原点の幅方向座標

                if (h_base == img->height-1 || w_base == img->width-1) {
                    // 補間原点が画像の端であるとき
                    // 補間できないので0を入れておく
                    new_img->image[i][j] = 0;
                } else {
                    new_img->image[i][j] = (uint)(
                        img->image[h_base][w_base]*(1-h_dist)*(1-w_dist) +
                        img->image[h_base+1][w_base]*h_dist*(1-w_dist) +
                        img->image[h_base][w_base+1]*(1-h_dist)*w_dist +
                        img->image[h_base+1][w_base+1]*h_dist*w_dist
                    );
                }
            } else {
                // 元の点は存在しないので0を入れておく
                new_img->image[i][j] = 0;
            }
        }
    }

    *img = *new_img;
    free(new_img);

    return true;
}

// 二値化
void binarize(PNM* img, uint th) {
    for (size_t i = 0; i < img->height; i++) {
        for (size_t j = 0; j < img->width; j++) {
            uint* px = &img->image[i][j];
            *px = *px > th ? img->max : 0;
        }
    }
}

// 二値化閾値の探索
uint find_threshold(const PNM* img) {
    const size_t total_px = img->width * img->height;
    const uint max = img->max;

    double* omega = malloc(sizeof(double) * (max + 1));
    double* mu = malloc(sizeof(double) * (max + 1));

    {
        // 全ての画素値について、その値を持つ画素の数を求める
        size_t* ni = calloc(max+1,sizeof(size_t));
        for(size_t i = 0; i < img->height; i++) {
            for(size_t j = 0; j < img->width; j++) {
                ni[img->image[i][j]]++;
            }
        }

        // omega と mu を漸化式を利用して求める
        // 浮動小数点数の加算を行うので整数演算を用いたナイーブな方法に比べて
        // 誤差が生じる可能性がある
        // max = 255 では誤差は小数点以下第15桁目かそれ以下に留まり
        // 実用上問題ない
        omega[0] = (double)ni[0]/total_px;
        mu[0] = 0;
        for(uint i = 1; i <= max; i++) {
            omega[i] = omega[i-1] + (double)ni[i]/total_px;
            mu[i] = mu[i-1] + (double)(i*ni[i])/total_px;
        }

        // 漸化式を使わない場合
        /*
        for(uint i = 0; i <= max; i++) {
            unsigned long long omega_tmp = 0;
            unsigned long long mu_tmp = 0;
            for (uint j = 0; j <= i; j++) {
                omega_tmp += ni[j];
                mu_tmp += j*ni[j];
            }
            omega[i] = (double)omega_tmp/total_px;
            mu[i] = (double)mu_tmp/total_px;
        }
        */

        free(ni);
    }

    // 最大の分散をとる画素値を見つける
    double max_var = 0;
    uint max_var_val = max;
    for(uint i = 0; i <= max; i++) {
        // 閾値より小さいクラスの要素がないときスキップする
        // (この閾値では正しく区分できていないし、ゼロ除算が起こるため)
        if (omega[i] == 0) continue;

        // 閾値より大きいクラスの要素がないとき処理を終了する
        // (閾値をこれ以上大きくしても変化はないし、ゼロ除算が起こるため)
        if (omega[i] == 1) break;

        // クラス間分散
        const double var =
            (mu[max]*omega[i]-mu[i])*(mu[max]*omega[i]-mu[i])
            / (omega[i]*(1-omega[i]));

        if (var > max_var) {
            max_var = var;
            max_var_val = i;
        }
    }

    free(omega);
    free(mu);

    return max_var_val;
}

// 座標
typedef struct {
    size_t y;
    size_t x;
} Point;

// 白画素の周囲の画素を再帰的にラベリングする
bool label_region(PNM* img, size_t y, size_t x, uint l_val) {
    Point* queue = malloc(QUEUE_SIZE*sizeof(Point));
    size_t front = 0, rear = 0;

#define ENQ(_y, _x) do {\
    if (rear - front == QUEUE_SIZE) {\
        free(queue);\
        return false;\
    }\
    queue[(rear++)%QUEUE_SIZE] = (Point){.y = (_y), .x = (_x)};\
    img->image[(_y)][(_x)] = l_val;\
} while (0)
#define DEQ() queue[(front++)%QUEUE_SIZE]

    ENQ(y, x);

    do {
        // キューから画素座標を取り出して、その周囲の画素値を調べる
        const Point p = DEQ();
        if (p.y >= 1) {
            if (p.x >= 1            && img->image[p.y-1][p.x-1] == img->max) ENQ(p.y-1, p.x-1);
            if (                       img->image[p.y-1][p.x]   == img->max) ENQ(p.y-1, p.x  );
            if (p.x <= img->width-2 && img->image[p.y-1][p.x+1] == img->max) ENQ(p.y-1, p.x+1);
        }

        if (p.x >= 1            && img->image[p.y][p.x-1] == img->max)       ENQ(p.y,   p.x-1);
        if (p.x <= img->width-2 && img->image[p.y][p.x+1] == img->max)       ENQ(p.y,   p.x+1);

        if (p.y <= img->height-2) {
            if (p.x >= 1            && img->image[p.y+1][p.x-1] == img->max) ENQ(p.y+1, p.x-1);
            if (                       img->image[p.y+1][p.x]   == img->max) ENQ(p.y+1, p.x  );
            if (p.x <= img->width-2 && img->image[p.y+1][p.x+1] == img->max) ENQ(p.y+1, p.x+1);
        }
    } while (front != rear);
#undef ENQ
#undef DEQ
    free(queue);
    return true;
}

// 画像内の連続した白色領域をそれぞれラベリングする
bool label_all(PNM* img) {
    uint l_val = 1;
    for (size_t i = 0; i < img->height; i++) {
        for (size_t j = 0; j < img->width; j++) {
            if (img->image[i][j] == img->max) {
                fprintf(stderr, "New region found, label %u\n", l_val);
                if (!label_region(img, i, j, l_val++)) {
                    fprintf(stderr, "label_all: queue overflowed, consider increasing QUEUE_SIZE");\
                    return false;
                }
                if (l_val == img->max) {
                    fprintf(stderr, "label_all: label reached max");\
                    return false;
                }
            }
        }
    }
    return true;
}


int main(int argc, char** argv) {

    /*
#define GET_DOUBLE_ARG(n, to, arg_desc)\
    if (!get_double(argv[(n)], (to))) {\
        fprintf(stderr, "main: error in parsing "arg_desc"\n");\
        return 1;\
    }
    */

    const int nargs = 3;
    if (argc != nargs) {
        fprintf(stderr, "expected %d arguments, got %d\n", nargs, argc);
        fprintf(stderr, "%s [input] [output]\n", argv[0]);
        return 0;
    }

    // 画素配列は大きいのでヒープに置く
    PNM* img = malloc(sizeof(PNM));

    if (!read_image(argv[1], img)) {
        fprintf(stderr, "main: error in reading image\n");
        return 1;
    }

    if (!label_all(img)) {
        fprintf(stderr, "main: error in labeling\n");
        return 1;
    }

    if (!write_image(argv[2], img)) {
        fprintf(stderr, "main: error in writing image\n");
        return 1;
    }
}
