#define H263_INTRA_DC_SIZE 254
#define H263_INTRA_DC_EVENT_DIM 1

#define H263_INTRA_DC_EVENT {\
8,\
16,\
24,\
32,\
40,\
48,\
56,\
64,\
72,\
80,\
88,\
96,\
104,\
112,\
120,\
128,\
136,\
144,\
152,\
160,\
168,\
176,\
184,\
192,\
200,\
208,\
216,\
224,\
232,\
240,\
248,\
256,\
264,\
272,\
280,\
288,\
296,\
304,\
312,\
320,\
328,\
336,\
344,\
352,\
360,\
368,\
376,\
384,\
392,\
400,\
408,\
416,\
424,\
432,\
440,\
448,\
456,\
464,\
472,\
480,\
488,\
496,\
504,\
512,\
520,\
528,\
536,\
544,\
552,\
560,\
568,\
576,\
584,\
592,\
600,\
608,\
616,\
624,\
632,\
640,\
648,\
656,\
664,\
672,\
680,\
688,\
696,\
704,\
712,\
720,\
728,\
736,\
744,\
752,\
760,\
768,\
776,\
784,\
792,\
800,\
808,\
816,\
824,\
832,\
840,\
848,\
856,\
864,\
872,\
880,\
888,\
896,\
904,\
912,\
920,\
928,\
936,\
944,\
952,\
960,\
968,\
976,\
984,\
992,\
1000,\
1008,\
1016,\
1024,\
1032,\
1040,\
1048,\
1056,\
1064,\
1072,\
1080,\
1088,\
1096,\
1104,\
1112,\
1120,\
1128,\
1136,\
1144,\
1152,\
1160,\
1168,\
1176,\
1184,\
1192,\
1200,\
1208,\
1216,\
1224,\
1232,\
1240,\
1248,\
1256,\
1264,\
1272,\
1280,\
1288,\
1296,\
1304,\
1312,\
1320,\
1328,\
1336,\
1344,\
1352,\
1360,\
1368,\
1376,\
1384,\
1392,\
1400,\
1408,\
1416,\
1424,\
1432,\
1440,\
1448,\
1456,\
1464,\
1472,\
1480,\
1488,\
1496,\
1504,\
1512,\
1520,\
1528,\
1536,\
1544,\
1552,\
1560,\
1568,\
1576,\
1584,\
1592,\
1600,\
1608,\
1616,\
1624,\
1632,\
1640,\
1648,\
1656,\
1664,\
1672,\
1680,\
1688,\
1696,\
1704,\
1712,\
1720,\
1728,\
1736,\
1744,\
1752,\
1760,\
1768,\
1776,\
1784,\
1792,\
1800,\
1808,\
1816,\
1824,\
1832,\
1840,\
1848,\
1856,\
1864,\
1872,\
1880,\
1888,\
1896,\
1904,\
1912,\
1920,\
1928,\
1936,\
1944,\
1952,\
1960,\
1968,\
1976,\
1984,\
1992,\
2000,\
2008,\
2016,\
2024,\
2032}


#define H263_INTRA_DC_BITS {\
"00000001",\
"00000010",\
"00000011",\
"00000100",\
"00000101",\
"00000110",\
"00000111",\
"00001000",\
"00001001",\
"00001010",\
"00001011",\
"00001100",\
"00001101",\
"00001110",\
"00001111",\
"00010000",\
"00010001",\
"00010010",\
"00010011",\
"00010100",\
"00010101",\
"00010110",\
"00010111",\
"00011000",\
"00011001",\
"00011010",\
"00011011",\
"00011100",\
"00011101",\
"00011110",\
"00011111",\
"00100000",\
"00100001",\
"00100010",\
"00100011",\
"00100100",\
"00100101",\
"00100110",\
"00100111",\
"00101000",\
"00101001",\
"00101010",\
"00101011",\
"00101100",\
"00101101",\
"00101110",\
"00101111",\
"00110000",\
"00110001",\
"00110010",\
"00110011",\
"00110100",\
"00110101",\
"00110110",\
"00110111",\
"00111000",\
"00111001",\
"00111010",\
"00111011",\
"00111100",\
"00111101",\
"00111110",\
"00111111",\
"01000000",\
"01000001",\
"01000010",\
"01000011",\
"01000100",\
"01000101",\
"01000110",\
"01000111",\
"01001000",\
"01001001",\
"01001010",\
"01001011",\
"01001100",\
"01001101",\
"01001110",\
"01001111",\
"01010000",\
"01010001",\
"01010010",\
"01010011",\
"01010100",\
"01010101",\
"01010110",\
"01010111",\
"01011000",\
"01011001",\
"01011010",\
"01011011",\
"01011100",\
"01011101",\
"01011110",\
"01011111",\
"01100000",\
"01100001",\
"01100010",\
"01100011",\
"01100100",\
"01100101",\
"01100110",\
"01100111",\
"01101000",\
"01101001",\
"01101010",\
"01101011",\
"01101100",\
"01101101",\
"01101110",\
"01101111",\
"01110000",\
"01110001",\
"01110010",\
"01110011",\
"01110100",\
"01110101",\
"01110110",\
"01110111",\
"01111000",\
"01111001",\
"01111010",\
"01111011",\
"01111100",\
"01111101",\
"01111110",\
"01111111",\
"11111111",\
"10000001",\
"10000010",\
"10000011",\
"10000100",\
"10000101",\
"10000110",\
"10000111",\
"10001000",\
"10001001",\
"10001010",\
"10001011",\
"10001100",\
"10001101",\
"10001110",\
"10001111",\
"10010000",\
"10010001",\
"10010010",\
"10010011",\
"10010100",\
"10010101",\
"10010110",\
"10010111",\
"10011000",\
"10011001",\
"10011010",\
"10011011",\
"10011100",\
"10011101",\
"10011110",\
"10011111",\
"10100000",\
"10100001",\
"10100010",\
"10100011",\
"10100100",\
"10100101",\
"10100110",\
"10100111",\
"10101000",\
"10101001",\
"10101010",\
"10101011",\
"10101100",\
"10101101",\
"10101110",\
"10101111",\
"10110000",\
"10110001",\
"10110010",\
"10110011",\
"10110100",\
"10110101",\
"10110110",\
"10110111",\
"10111000",\
"10111001",\
"10111010",\
"10111011",\
"10111100",\
"10111101",\
"10111110",\
"10111111",\
"11000000",\
"11000001",\
"11000010",\
"11000011",\
"11000100",\
"11000101",\
"11000110",\
"11000111",\
"11001000",\
"11001001",\
"11001010",\
"11001011",\
"11001100",\
"11001101",\
"11001110",\
"11001111",\
"11010000",\
"11010001",\
"11010010",\
"11010011",\
"11010100",\
"11010101",\
"11010110",\
"11010111",\
"11011000",\
"11011001",\
"11011010",\
"11011011",\
"11011100",\
"11011101",\
"11011110",\
"11011111",\
"11100000",\
"11100001",\
"11100010",\
"11100011",\
"11100100",\
"11100101",\
"11100110",\
"11100111",\
"11101000",\
"11101001",\
"11101010",\
"11101011",\
"11101100",\
"11101101",\
"11101110",\
"11101111",\
"11110000",\
"11110001",\
"11110010",\
"11110011",\
"11110100",\
"11110101",\
"11110110",\
"11110111",\
"11111000",\
"11111001",\
"11111010",\
"11111011",\
"11111100",\
"11111101",\
"11111110"}