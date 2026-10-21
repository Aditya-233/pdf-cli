#define _POSIX_C_SOURCE 200809L
#include "pdfcli/img_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uint8_t smart_inv_lut[256] = {
    255, 254, 253, 252, 251, 251, 250, 249, 248, 247, 246, 245, 244, 244, 243, 242,
    241, 240, 239, 238, 237, 237, 236, 235, 234, 233, 232, 231, 230, 229, 229, 228,
    227, 226, 225, 224, 223, 222, 222, 221, 220, 219, 218, 217, 216, 215, 215, 214,
    213, 212, 211, 210, 209, 208, 207, 207, 206, 205, 204, 203, 202, 201, 200, 200,
    199, 198, 197, 196, 195, 194, 193, 193, 192, 191, 190, 189, 188, 187, 186, 185,
    185, 184, 183, 182, 181, 180, 179, 178, 178, 177, 176, 175, 174, 173, 172, 171,
    171, 170, 169, 168, 167, 166, 165, 164, 163, 163, 162, 161, 160, 159, 158, 157,
    156, 156, 155, 154, 153, 152, 151, 150, 149, 149, 148, 147, 146, 145, 144, 143,
    142, 141, 141, 140, 139, 138, 137, 136, 135, 134, 134, 133, 132, 131, 130, 129,
    128, 127, 127, 126, 125, 124, 123, 122, 121, 120, 119, 119, 118, 117, 116, 115,
    114, 113, 112, 112, 111, 110, 109, 108, 107, 106, 105, 105, 104, 103, 102, 101,
    100,  99,  98,  97,  97,  96,  95,  94,  93,  92,  91,  90,  90,  89,  88,  87,
     86,  85,  84,  83,  83,  82,  81,  80,  79,  78,  77,  76,  75,  75,  74,  73,
     72,  71,  70,  69,  68,  68,  67,  66,  65,  64,  63,  62,  61,  61,  60,  59,
     58,  57,  56,  55,  54,  53,  53,  52,  51,  50,  49,  48,  47,  46,  46,  45,
     44,  43,  42,  41,  40,  39,  39,  38,  37,  36,  35,  34,  33,  32,  31,  31
};

static const int32_t smart_inv_offset[512] = {
       16711680,    16654008,    16596337,    16538665,    16480993,    16423322,    16365650,    16307978,
       16250307,    16192635,    16134963,    16077292,    16019620,    15961948,    15904276,    15846605,
       15788933,    15731261,    15673590,    15615918,    15558246,    15500575,    15442903,    15385231,
       15327560,    15269888,    15212216,    15154545,    15096873,    15039201,    14981530,    14923858,
       14866186,    14808515,    14750843,    14693171,    14635500,    14577828,    14520156,    14462484,
       14404813,    14347141,    14289469,    14231798,    14174126,    14116454,    14058783,    14001111,
       13943439,    13885768,    13828096,    13770424,    13712753,    13655081,    13597409,    13539738,
       13482066,    13424394,    13366723,    13309051,    13251379,    13193708,    13136036,    13078364,
       13020692,    12963021,    12905349,    12847677,    12790006,    12732334,    12674662,    12616991,
       12559319,    12501647,    12443976,    12386304,    12328632,    12270961,    12213289,    12155617,
       12097946,    12040274,    11982602,    11924931,    11867259,    11809587,    11751916,    11694244,
       11636572,    11578900,    11521229,    11463557,    11405885,    11348214,    11290542,    11232870,
       11175199,    11117527,    11059855,    11002184,    10944512,    10886840,    10829169,    10771497,
       10713825,    10656154,    10598482,    10540810,    10483139,    10425467,    10367795,    10310124,
       10252452,    10194780,    10137108,    10079437,    10021765,     9964093,     9906422,     9848750,
        9791078,     9733407,     9675735,     9618063,     9560392,     9502720,     9445048,     9387377,
        9329705,     9272033,     9214362,     9156690,     9099018,     9041347,     8983675,     8926003,
        8868332,     8810660,     8752988,     8695316,     8637645,     8579973,     8522301,     8464630,
        8406958,     8349286,     8291615,     8233943,     8176271,     8118600,     8060928,     8003256,
        7945585,     7887913,     7830241,     7772570,     7714898,     7657226,     7599555,     7541883,
        7484211,     7426540,     7368868,     7311196,     7253524,     7195853,     7138181,     7080509,
        7022838,     6965166,     6907494,     6849823,     6792151,     6734479,     6676808,     6619136,
        6561464,     6503793,     6446121,     6388449,     6330778,     6273106,     6215434,     6157763,
        6100091,     6042419,     5984748,     5927076,     5869404,     5811732,     5754061,     5696389,
        5638717,     5581046,     5523374,     5465702,     5408031,     5350359,     5292687,     5235016,
        5177344,     5119672,     5062001,     5004329,     4946657,     4888986,     4831314,     4773642,
        4715971,     4658299,     4600627,     4542956,     4485284,     4427612,     4369940,     4312269,
        4254597,     4196925,     4139254,     4081582,     4023910,     3966239,     3908567,     3850895,
        3793224,     3735552,     3677880,     3620209,     3562537,     3504865,     3447194,     3389522,
        3331850,     3274179,     3216507,     3158835,     3101164,     3043492,     2985820,     2928148,
        2870477,     2812805,     2755133,     2697462,     2639790,     2582118,     2524447,     2466775,
        2409103,     2351432,     2293760,     2236088,     2178417,     2120745,     2063073,     2005402,
        1889604,     1772891,     1655252,     1536676,     1417150,     1296665,     1175208,     1052768,
         929332,      804889,      679426,      552930,      425388,      296788,      167117,       36360,
         -95494,     -228462,     -362557,     -497794,     -634186,     -771749,     -910497,    -1050447,
       -1191614,    -1334014,    -1477663,    -1622578,    -1768775,    -1916272,    -2065085,    -2215233,
       -2366734,    -2519606,    -2656590,    -2735490,    -2814584,    -2893875,    -2973367,    -3053062,
       -3132963,    -3213072,    -3293393,    -3373929,    -3454682,    -3535657,    -3616855,    -3698281,
       -3779937,    -3861828,    -3943955,    -4026324,    -4108937,    -4191799,    -4274912,    -4358281,
       -4441910,    -4525802,    -4609962,    -4694393,    -4779100,    -4864088,    -4949360,    -5034921,
       -5120775,    -5206928,    -5293384,    -5380147,    -5467224,    -5554618,    -5642335,    -5730381,
       -5818760,    -5907479,    -5996543,    -6085958,    -6175729,    -6265863,    -6356365,    -6447243,
       -6538503,    -6630151,    -6722194,    -6814639,    -6907493,    -7000764,    -7094458,    -7188584,
       -7283149,    -7378161,    -7473629,    -7569560,    -7665963,    -7762848,    -7860223,    -7958097,
       -8056481,    -8155382,    -8254813,    -8354782,    -8455301,    -8556379,    -8658029,    -8760261,
       -8863088,    -8966520,    -9070571,    -9175253,    -9280579,    -9386562,    -9493216,    -9600556,
       -9708594,    -9817348,    -9926831,   -10037059,   -10148049,   -10259818,   -10372382,   -10485759,
      -10599968,   -10715027,   -10830955,   -10947773,   -11065501,   -11184159,   -11303771,   -11424358,
      -11545944,   -11668553,   -11792209,   -11916937,   -12042765,   -12169720,   -12297829,   -12427123,
      -12557630,   -12689382,   -12822411,   -12956751,   -13092436,   -13229502,   -13367985,   -13507925,
      -13649360,   -13792333,   -13936885,   -14083061,   -14230906,   -14380470,   -14531800,   -14684950,
      -14839971,   -14996920,   -15155854,   -15316835,   -15479923,   -15645185,   -15812689,   -15982505,
      -16154707,   -16329372,   -16506580,   -16686415,   -16868965,   -17054322,   -17242580,   -17433841,
      -17628208,   -17825791,   -18026706,   -18231072,   -18439016,   -18650671,   -18866175,   -19085675,
      -19309324,   -19537285,   -19769727,   -20006829,   -20248781,   -20495782,   -20748041,   -21005782,
      -21269240,   -21538662,   -21814312,   -22096469,   -22385428,   -22681504,   -22985031,   -23296362,
      -23615875,   -23943974,   -24281087,   -24627672,   -24984220,   -25351255,   -25729339,   -26119074,
      -26521107,   -26936136,   -27364909,   -27808235,   -28266987,   -28742109,   -29234626,   -29745646,
      -30276377,   -30828133,   -31402348,   -32000588,   -32624569,   -33276175,   -33957477,   -34670761,
      -35418551,   -36203644,   -37029150,   -37898532,   -38815661,   -39784879,   -40811068,   -41899743,
      -43057151,   -44290403,   -45607625,   -47018147,   -48532734,   -50163875,   -51926138,   -53836626,
      -55915553,   -58186979,   -60679781,   -63428913,   -66477096,   -69877104,   -73694903,   -78014053,
      -82941986,   -88619190,   -95232982,  -103038796,  -112394239,  -123815853,  -138078453,  -156399603,
     -180808580,  -214958079,  -266153491,  -351440731,  -521957539, -1033392619,     2005402,           0
};

static const int32_t smart_inv_k[512] = {
              0,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          57672,       57672,       57672,       57672,       57672,       57672,       57672,       57672,
          58126,       58583,       59045,       59510,       59979,       60451,       60927,       61407,
          61892,       62380,       62872,       63368,       63868,       64372,       64881,       65393,
          65910,       66432,       66958,       67488,       68023,       68562,       69107,       69655,
          70209,       70767,       71331,       71899,       72472,       73051,       73634,       74223,
          74817,       75417,       75903,       75986,       76070,       76155,       76240,       76327,
          76414,       76502,       76591,       76680,       76771,       76862,       76954,       77048,
          77142,       77237,       77332,       77429,       77527,       77626,       77726,       77826,
          77928,       78031,       78135,       78240,       78346,       78453,       78561,       78671,
          78781,       78893,       79006,       79120,       79235,       79352,       79470,       79589,
          79709,       79831,       79954,       80078,       80204,       80332,       80460,       80591,
          80722,       80856,       80990,       81127,       81265,       81404,       81546,       81688,
          81833,       81980,       82128,       82278,       82430,       82584,       82739,       82897,
          83057,       83218,       83382,       83548,       83716,       83886,       84059,       84233,
          84410,       84590,       84772,       84956,       85143,       85332,       85524,       85719,
          85917,       86117,       86320,       86526,       86735,       86948,       87163,       87381,
          87603,       87828,       88057,       88288,       88524,       88763,       89006,       89253,
          89503,       89758,       90017,       90280,       90547,       90819,       91095,       91376,
          91662,       91952,       92248,       92548,       92854,       93166,       93482,       93805,
          94134,       94468,       94809,       95156,       95509,       95870,       96237,       96612,
          96993,       97383,       97780,       98185,       98598,       99020,       99451,       99891,
         100340,      100799,      101267,      101746,      102236,      102737,      103249,      103773,
         104309,      104858,      105419,      105995,      106584,      107188,      107807,      108441,
         109092,      109760,      110445,      111149,      111872,      112614,      113377,      114162,
         114969,      115799,      116654,      117534,      118441,      119376,      120340,      121335,
         122362,      123423,      124518,      125651,      126823,      128037,      129293,      130595,
         131946,      133347,      134803,      136315,      137888,      139525,      141230,      143008,
         144863,      146801,      148826,      150946,      153167,      155496,      157942,      160513,
         163219,      166072,      169083,      172266,      175636,      179211,      183009,      187052,
         191365,      195975,      200915,      206220,      211933,      218104,      224788,      232054,
         239981,      248662,      258212,      268767,      280494,      293601,      308347,      325059,
         344158,      366195,      391905,      422290,      458752,      503316,      559022,      630644,
         726139,      859832,     1060372,     1394606,     2063073,     4068475,           0,           0
};


[[nodiscard]] fz_pixmap *img_crop(fz_context *ctx, fz_pixmap *pix, const crop_box *crop) {
    if (!ctx || !pix) return nullptr;
    if (!crop || (crop->top == 0.0 && crop->bottom == 0.0 && crop->left == 0.0 && crop->right == 0.0))
        return fz_clone_pixmap(ctx, pix);
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int x0 = (int)(w * crop->left), x1 = w - (int)(w * crop->right);
    int y0 = (int)(h * crop->top), y1 = h - (int)(h * crop->bottom);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return fz_clone_pixmap(ctx, pix);
    int nw = x1 - x0, nh = y1 - y0;
    fz_pixmap *dst = nullptr;
    fz_try(ctx) {
        dst = fz_new_pixmap_with_bbox(ctx, fz_pixmap_colorspace(ctx, pix), fz_make_irect(0, 0, nw, nh), nullptr, fz_pixmap_alpha(ctx, pix));
        int n = fz_pixmap_components(ctx, pix);
        int src_stride = fz_pixmap_stride(ctx, pix), dst_stride = fz_pixmap_stride(ctx, dst);
        const unsigned char *src = fz_pixmap_samples(ctx, pix);
        unsigned char *dst_s = fz_pixmap_samples(ctx, dst);
        size_t row_bytes = (size_t)nw * (size_t)n;
        if (row_bytes == (size_t)src_stride && row_bytes == (size_t)dst_stride) {
            memcpy(dst_s, src + (size_t)y0 * (size_t)src_stride, (size_t)nh * row_bytes);
        } else {
            const unsigned char *s_row = src + (size_t)y0 * (size_t)src_stride + (size_t)x0 * (size_t)n;
            unsigned char *d_row = dst_s;
            for (int y = 0; y < nh; y++) {
                memcpy(d_row, s_row, row_bytes);
                s_row += src_stride;
                d_row += dst_stride;
            }
        }
    } fz_catch(ctx) { if (dst) fz_drop_pixmap(ctx, dst); dst = nullptr; }
    return dst;
}

static void apply_lut(fz_context *ctx, fz_pixmap *pix, const uint8_t lut[256]) {
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    if (!s || w <= 0 || h <= 0) return;

    if (n == 4) {
        if (stride == w * 4) {
            size_t total_pix = (size_t)w * (size_t)h;
            size_t i = 0;
            #pragma GCC ivdep
            for (; i + 2 <= total_pix; i += 2) {
                uint64_t v;
                memcpy(&v, s + i * 4, sizeof(v));
                uint64_t out = (uint64_t)lut[v & 0xFF]
                             | ((uint64_t)lut[(v >> 8) & 0xFF] << 8)
                             | ((uint64_t)lut[(v >> 16) & 0xFF] << 16)
                             | ((uint64_t)lut[(v >> 32) & 0xFF] << 32)
                             | ((uint64_t)lut[(v >> 40) & 0xFF] << 40)
                             | ((uint64_t)lut[(v >> 48) & 0xFF] << 48)
                             | (v & 0xFF000000FF000000ULL);
                memcpy(s + i * 4, &out, sizeof(out));
            }
            if (i < total_pix) {
                uint32_t v;
                memcpy(&v, s + i * 4, sizeof(v));
                uint32_t out = (uint32_t)lut[v & 0xFF]
                             | ((uint32_t)lut[(v >> 8) & 0xFF] << 8)
                             | ((uint32_t)lut[(v >> 16) & 0xFF] << 16)
                             | (v & 0xFF000000U);
                memcpy(s + i * 4, &out, sizeof(out));
            }
        } else {
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                size_t x = 0;
                #pragma GCC ivdep
                for (; x + 2 <= (size_t)w; x += 2) {
                    uint64_t v;
                    memcpy(&v, row + x * 4, sizeof(v));
                    uint64_t out = (uint64_t)lut[v & 0xFF]
                                 | ((uint64_t)lut[(v >> 8) & 0xFF] << 8)
                                 | ((uint64_t)lut[(v >> 16) & 0xFF] << 16)
                                 | ((uint64_t)lut[(v >> 32) & 0xFF] << 32)
                                 | ((uint64_t)lut[(v >> 40) & 0xFF] << 40)
                                 | ((uint64_t)lut[(v >> 48) & 0xFF] << 48)
                                 | (v & 0xFF000000FF000000ULL);
                    memcpy(row + x * 4, &out, sizeof(out));
                }
                if (x < (size_t)w) {
                    uint32_t v;
                    memcpy(&v, row + x * 4, sizeof(v));
                    uint32_t out = (uint32_t)lut[v & 0xFF]
                                 | ((uint32_t)lut[(v >> 8) & 0xFF] << 8)
                                 | ((uint32_t)lut[(v >> 16) & 0xFF] << 16)
                                 | (v & 0xFF000000U);
                    memcpy(row + x * 4, &out, sizeof(out));
                }
            }
        }
    } else if (n == 3 || n == 1) {
        if (stride == w * n) {
            size_t total_bytes = (size_t)w * (size_t)h * (size_t)n;
            size_t i = 0;
            #pragma GCC ivdep
            for (; i + 8 <= total_bytes; i += 8) {
                uint64_t v;
                memcpy(&v, s + i, sizeof(v));
                uint64_t out = (uint64_t)lut[v & 0xFF]
                             | ((uint64_t)lut[(v >> 8) & 0xFF] << 8)
                             | ((uint64_t)lut[(v >> 16) & 0xFF] << 16)
                             | ((uint64_t)lut[(v >> 24) & 0xFF] << 24)
                             | ((uint64_t)lut[(v >> 32) & 0xFF] << 32)
                             | ((uint64_t)lut[(v >> 40) & 0xFF] << 40)
                             | ((uint64_t)lut[(v >> 48) & 0xFF] << 48)
                             | ((uint64_t)lut[(v >> 56) & 0xFF] << 56);
                memcpy(s + i, &out, sizeof(out));
            }
            #pragma GCC ivdep
            for (; i < total_bytes; i++) {
                s[i] = lut[s[i]];
            }
        } else {
            size_t row_bytes = (size_t)w * (size_t)n;
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                size_t x = 0;
                #pragma GCC ivdep
                for (; x + 8 <= row_bytes; x += 8) {
                    uint64_t v;
                    memcpy(&v, row + x, sizeof(v));
                    uint64_t out = (uint64_t)lut[v & 0xFF]
                                 | ((uint64_t)lut[(v >> 8) & 0xFF] << 8)
                                 | ((uint64_t)lut[(v >> 16) & 0xFF] << 16)
                                 | ((uint64_t)lut[(v >> 24) & 0xFF] << 24)
                                 | ((uint64_t)lut[(v >> 32) & 0xFF] << 32)
                                 | ((uint64_t)lut[(v >> 40) & 0xFF] << 40)
                                 | ((uint64_t)lut[(v >> 48) & 0xFF] << 48)
                                 | ((uint64_t)lut[(v >> 56) & 0xFF] << 56);
                    memcpy(row + x, &out, sizeof(out));
                }
                #pragma GCC ivdep
                for (; x < row_bytes; x++) {
                    row[x] = lut[row[x]];
                }
            }
        }
    } else {
        int channels = (n >= 3) ? 3 : 1;
        for (int y = 0; y < h; y++) {
            unsigned char *row = s + (size_t)y * (size_t)stride;
            #pragma GCC ivdep
            for (int x = 0; x < w; x++) {
                unsigned char *p = row + x * n;
                for (int c = 0; c < channels; c++) p[c] = lut[p[c]];
            }
        }
    }
}

void img_smart_invert(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    if (!s || w <= 0 || h <= 0) return;

    if (n < 3) {
        apply_lut(ctx, pix, smart_inv_lut);
        return;
    }

    if (stride == w * n) {
        size_t total_pix = (size_t)w * (size_t)h;
        #pragma GCC ivdep
        for (size_t i = 0; i < total_pix; i++) {
            unsigned char *p = s + i * n;
            uint8_t r = p[0], g = p[1], b = p[2];
            if (r == g && g == b) {
                uint8_t inv = smart_inv_lut[r];
                p[0] = inv;
                p[1] = inv;
                p[2] = inv;
            } else {
                uint8_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
                uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                int sum = (int)mn + (int)mx;
                int64_t off = smart_inv_offset[sum];
                int64_t k = smart_inv_k[sum];
                int nr = (int)((off + k * r + 32768) >> 16);
                int ng = (int)((off + k * g + 32768) >> 16);
                int nb = (int)((off + k * b + 32768) >> 16);
                p[0] = (uint8_t)(nr < 0 ? 0 : (nr > 255 ? 255 : nr));
                p[1] = (uint8_t)(ng < 0 ? 0 : (ng > 255 ? 255 : ng));
                p[2] = (uint8_t)(nb < 0 ? 0 : (nb > 255 ? 255 : nb));
            }
        }
    } else {
        for (int y = 0; y < h; y++) {
            unsigned char *row = s + (size_t)y * (size_t)stride;
            #pragma GCC ivdep
            for (int x = 0; x < w; x++) {
                unsigned char *p = row + x * n;
                uint8_t r = p[0], g = p[1], b = p[2];
                if (r == g && g == b) {
                    uint8_t inv = smart_inv_lut[r];
                    p[0] = inv;
                    p[1] = inv;
                    p[2] = inv;
                } else {
                    uint8_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
                    uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                    int sum = (int)mn + (int)mx;
                    int64_t off = smart_inv_offset[sum];
                    int64_t k = smart_inv_k[sum];
                    int nr = (int)((off + k * r + 32768) >> 16);
                    int ng = (int)((off + k * g + 32768) >> 16);
                    int nb = (int)((off + k * b + 32768) >> 16);
                    p[0] = (uint8_t)(nr < 0 ? 0 : (nr > 255 ? 255 : nr));
                    p[1] = (uint8_t)(ng < 0 ? 0 : (ng > 255 ? 255 : ng));
                    p[2] = (uint8_t)(nb < 0 ? 0 : (nb > 255 ? 255 : nb));
                }
            }
        }
    }
}

void img_simple_invert(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    if (!s || w <= 0 || h <= 0) return;

    if (n == 4) {
        if (stride == w * 4) {
            size_t total_pix = (size_t)w * (size_t)h;
            size_t i = 0;
            #pragma GCC ivdep
            for (; i + 2 <= total_pix; i += 2) {
                uint64_t pix64;
                memcpy(&pix64, s + i * 4, sizeof(pix64));
                pix64 ^= 0x00FFFFFF00FFFFFFULL;
                memcpy(s + i * 4, &pix64, sizeof(pix64));
            }
            if (i < total_pix) {
                uint32_t pix32;
                memcpy(&pix32, s + i * 4, sizeof(pix32));
                pix32 ^= 0x00FFFFFFU;
                memcpy(s + i * 4, &pix32, sizeof(pix32));
            }
        } else {
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                size_t x = 0;
                #pragma GCC ivdep
                for (; x + 2 <= (size_t)w; x += 2) {
                    uint64_t pix64;
                    memcpy(&pix64, row + x * 4, sizeof(pix64));
                    pix64 ^= 0x00FFFFFF00FFFFFFULL;
                    memcpy(row + x * 4, &pix64, sizeof(pix64));
                }
                if (x < (size_t)w) {
                    uint32_t pix32;
                    memcpy(&pix32, row + x * 4, sizeof(pix32));
                    pix32 ^= 0x00FFFFFFU;
                    memcpy(row + x * 4, &pix32, sizeof(pix32));
                }
            }
        }
    } else if (n == 3 || n == 1) {
        if (stride == w * n) {
            size_t total_bytes = (size_t)w * (size_t)h * (size_t)n;
            size_t i = 0;
            #pragma GCC ivdep
            for (; i + 8 <= total_bytes; i += 8) {
                uint64_t v;
                memcpy(&v, s + i, sizeof(v));
                v ^= 0xFFFFFFFFFFFFFFFFULL;
                memcpy(s + i, &v, sizeof(v));
            }
            #pragma GCC ivdep
            for (; i < total_bytes; i++) {
                s[i] ^= 0xFF;
            }
        } else {
            size_t row_bytes = (size_t)w * (size_t)n;
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                size_t x = 0;
                #pragma GCC ivdep
                for (; x + 8 <= row_bytes; x += 8) {
                    uint64_t v;
                    memcpy(&v, row + x, sizeof(v));
                    v ^= 0xFFFFFFFFFFFFFFFFULL;
                    memcpy(row + x, &v, sizeof(v));
                }
                #pragma GCC ivdep
                for (; x < row_bytes; x++) {
                    row[x] ^= 0xFF;
                }
            }
        }
    } else {
        int channels = (n >= 3) ? 3 : 1;
        for (int y = 0; y < h; y++) {
            unsigned char *row = s + (size_t)y * (size_t)stride;
            #pragma GCC ivdep
            for (int x = 0; x < w; x++) {
                unsigned char *p = row + x * n;
                for (int c = 0; c < channels; c++) p[c] ^= 0xFF;
            }
        }
    }
}

void img_dim_tint(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    if (!s || w <= 0 || h <= 0) return;

    if (n == 4) {
        if (stride == w * 4) {
            size_t total_pix = (size_t)w * (size_t)h;
            #pragma GCC ivdep
            for (size_t i = 0; i < total_pix; i++) {
                s[i * 4 + 0] = (uint8_t)(((uint32_t)s[i * 4 + 0] * 179) >> 8);
                s[i * 4 + 1] = (uint8_t)(((uint32_t)s[i * 4 + 1] * 179) >> 8);
                s[i * 4 + 2] = (uint8_t)(((uint32_t)s[i * 4 + 2] * 179) >> 8);
            }
        } else {
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                #pragma GCC ivdep
                for (int x = 0; x < w; x++) {
                    row[x * 4 + 0] = (uint8_t)(((uint32_t)row[x * 4 + 0] * 179) >> 8);
                    row[x * 4 + 1] = (uint8_t)(((uint32_t)row[x * 4 + 1] * 179) >> 8);
                    row[x * 4 + 2] = (uint8_t)(((uint32_t)row[x * 4 + 2] * 179) >> 8);
                }
            }
        }
    } else if (n == 3 || n == 1) {
        if (stride == w * n) {
            size_t total_bytes = (size_t)w * (size_t)h * (size_t)n;
            #pragma GCC ivdep
            for (size_t i = 0; i < total_bytes; i++) {
                s[i] = (uint8_t)(((uint32_t)s[i] * 179) >> 8);
            }
        } else {
            size_t row_bytes = (size_t)w * (size_t)n;
            for (int y = 0; y < h; y++) {
                unsigned char *row = s + (size_t)y * (size_t)stride;
                #pragma GCC ivdep
                for (size_t x = 0; x < row_bytes; x++) {
                    row[x] = (uint8_t)(((uint32_t)row[x] * 179) >> 8);
                }
            }
        }
    } else {
        int channels = (n >= 3) ? 3 : 1;
        for (int y = 0; y < h; y++) {
            unsigned char *row = s + (size_t)y * (size_t)stride;
            #pragma GCC ivdep
            for (int x = 0; x < w; x++) {
                unsigned char *p = row + x * n;
                for (int c = 0; c < channels; c++) {
                    p[c] = (uint8_t)(((uint32_t)p[c] * 179) >> 8);
                }
            }
        }
    }
}

[[nodiscard]] unsigned char *img_save_png_to_memory(fz_context *ctx, fz_pixmap *pix, size_t *out_sz) {
    if (!out_sz) return nullptr;
    *out_sz = 0;
    if (!ctx || !pix) return nullptr;
    fz_buffer *buf = nullptr;
    unsigned char *res = nullptr;
    fz_var(buf);
    fz_var(res);
    fz_try(ctx) {
        buf = fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params);
        if (buf) {
            unsigned char *data = nullptr;
            size_t sz = fz_buffer_storage(ctx, buf, &data);
            if (sz > 0 && data) {
                res = (unsigned char *)malloc(sz);
                if (res) {
                    memcpy(res, data, sz);
                    *out_sz = sz;
                }
            }
        }
    } fz_always(ctx) {
        if (buf) fz_drop_buffer(ctx, buf);
    } fz_catch(ctx) {
        free(res);
        res = nullptr;
        *out_sz = 0;
    }
    return res;
}

[[nodiscard]] bool img_save_png_to_file(fz_context *ctx, fz_pixmap *pix, const char *path) {
    if (!ctx || !pix || !path) return false;
    bool ok = true;
    fz_try(ctx) { fz_save_pixmap_as_png(ctx, pix, path); } fz_catch(ctx) { ok = false; }
    return ok;
}
