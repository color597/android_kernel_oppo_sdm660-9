/*
 * TI LM3697 Backlight Driver
 *
 * Copyright 2014 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/lm3697_bl.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/of_gpio.h>
#include <linux/gpio.h>

#ifdef VENDOR_EDIT
//YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2017/02/28,
//add for 2048 bl level too low in ftm mode
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>
#include <soc/oppo/boot_mode.h>
#endif /*VENDOR_EDIT*/

/* Registers */
#define LM3697_REG_OUTPUT_CFG       0x10

#define LM3697_REG_BRT_CFG      0x16

#define LM3697_REG_BOOST_CTL    0x1A
#define LM3697_REG_BOOST        0x06

#define LM3697_REG_PWM_CFG      0x1C

#define LM3697_REG_IMAX_A       0x17
#define LM3697_REG_IMAX_B       0x18

#define LM3697_REG_BRT_A_LSB        0x20
#define LM3697_REG_BRT_A_MSB        0x21
#define LM3697_REG_BRT_B_LSB        0x22
#define LM3697_REG_BRT_B_MSB        0x23
#define LM3697_BRT_LSB_MASK     (BIT(0) | BIT(1) | BIT(2))
#define LM3697_BRT_MSB_SHIFT        3

#define LM3697_REG_ENABLE       0x24

/* Other definitions */
#define LM3697_PWM_ID           1
#define LM3697_MAX_REGISTERS        0xB4
#define LM3697_MAX_STRINGS      3
#define LM3697_MAX_BRIGHTNESS       2047
#define LM3697_IMAX_OFFSET      6
#define LM3697_DEFAULT_NAME     "lcd-backlight"
#define LM3697_DEFAULT_PWM      "lm3697-backlight"

//YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/10/14,
//add for lm3697 bl change to exponential mode
#define LM3697_EXPONENTIAL      1
#define LM3697_LINEAR           0
#define LM3697_LEVEL_2048_SUPPROT    1

#ifdef VENDOR_EDIT
//YongPeng.Yi@SWDP.MultiMedia, 2016/09/01,  Add for cabc
#define REG_PWM     0x1C
//YongPeng.Yi@SWDP.MultiMedia, 2016/09/19,  Add for close cabc when bl <=160
#define CABC_DISABLE_LEVEL  160
#define LM3697_MIN_BRIGHTNESS 6
#define FILTER_STR 0x50
#define RUN_RAMP 0x08
#define REG_REVISION 0x1F
static bool pwm_flag = false;
static int backlight_level = 2047;

extern int cabc_mode;
int set_backlight_pwm(int state);
#endif /*VENDOR_EDIT*/


enum lm3697_bl_ctrl_mode {
    BL_REGISTER_BASED,
    BL_PWM_BASED,
};

/*
 * struct lm3697_bl_chip
 * @dev: Parent device structure
 * @regmap: Used for I2C register access
 * @pdata: LM3697 platform data
 */
struct lm3697_bl_chip {
    struct device *dev;
    struct lm3697_platform_data *pdata;
    struct regmap *regmap;

};

/*
 * struct lm3697_bl
 * @bank_id: Control bank ID. BANK A or BANK A and B
 * @bl_dev: Backlight device structure
 * @chip: LM3697 backlight chip structure for low level access
 * @bl_pdata: LM3697 backlight platform data
 * @mode: Backlight control mode
 * @pwm: PWM device structure. Only valid in PWM control mode
 * @pwm_name: PWM device name
 */
struct lm3697_bl {
    int bank_id;
    struct backlight_device *bl_dev;
    struct lm3697_bl_chip *chip;
    struct lm3697_backlight_platform_data *bl_pdata;
    enum lm3697_bl_ctrl_mode mode;
    struct pwm_device *pwm;
    char pwm_name[20];
};

static struct lm3697_bl_chip *lm3697_pchip;

//YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/10/14,
//add for lm3697 bl change to exponential mode
//YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/11/01,
//add for silence and sau mode bl isn't black
static int backlight_buf[] = {
    0, 650, 650, 670, 685, 698, 709, 719, 729, 738, 746, 753, 759, 764, 768, 771, 774, 777, 779, 780, \
    782, 784, 786, 788, 789, 791, 793, 795, 797, 798, 800, 802, 804, 805, 807, 809, 810, 812, 814, 815, \
    817, 819, 820, 822, 824, 825, 827, 829, 830, 832, 833, 835, 837, 838, 840, 841, 843, 844, 846, 847, \
    849, 851, 852, 854, 855, 857, 858, 860, 861, 863, 864, 866, 867, 868, 870, 871, 873, 874, 876, 877, \
    879, 880, 881, 883, 884, 886, 887, 888, 890, 891, 893, 894, 895, 897, 898, 899, 901, 902, 903, 905, \
    906, 907, 909, 910, 911, 913, 914, 915, 917, 918, 919, 921, 922, 923, 924, 926, 927, 928, 929, 931, \
    932, 933, 934, 936, 937, 938, 939, 941, 942, 943, 944, 946, 947, 948, 949, 950, 952, 953, 954, 955, \
    956, 958, 959, 960, 961, 962, 963, 965, 966, 967, 968, 969, 970, 971, 973, 974, 975, 976, 977, 978, \
    979, 981, 982, 983, 984, 985, 986, 987, 988, 989, 991, 992, 993, 994, 995, 996, 997, 998, 999, 1000, \
    1001, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 1015, 1016, 1017, 1018, 1019, 1020, 1021, \
    1022, 1023, 1025, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041, 1042, \
    1043, 1044, 1045, 1046, 1047, 1048, 1049, 1049, 1050, 1051, 1052, 1053, 1054, 1055, 1056, 1057, 1058, 1059, 1060, 1061, \
    1062, 1063, 1064, 1065, 1066, 1067, 1068, 1069, 1070, 1070, 1071, 1072, 1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080, \
    1081, 1082, 1082, 1083, 1084, 1085, 1086, 1087, 1088, 1089, 1090, 1091, 1092, 1092, 1093, 1094, 1095, 1096, 1097, 1098, \
    1099, 1100, 1100, 1101, 1102, 1103, 1104, 1105, 1106, 1107, 1107, 1108, 1109, 1110, 1111, 1112, 1113, 1114, 1114, 1115, \
    1116, 1117, 1118, 1119, 1120, 1120, 1121, 1122, 1123, 1124, 1125, 1126, 1126, 1127, 1128, 1129, 1130, 1131, 1131, 1132, \
    1133, 1134, 1135, 1136, 1136, 1137, 1138, 1139, 1140, 1141, 1141, 1142, 1143, 1144, 1145, 1145, 1146, 1147, 1148, 1149, \
    1150, 1150, 1151, 1152, 1153, 1154, 1154, 1155, 1156, 1157, 1158, 1158, 1159, 1160, 1161, 1162, 1162, 1163, 1164, 1165, \
    1166, 1166, 1167, 1168, 1169, 1169, 1170, 1171, 1172, 1173, 1173, 1174, 1175, 1176, 1176, 1177, 1178, 1179, 1180, 1180, \
    1181, 1182, 1183, 1183, 1184, 1185, 1186, 1186, 1187, 1188, 1189, 1190, 1190, 1191, 1192, 1193, 1193, 1194, 1195, 1196, \
    1196, 1197, 1198, 1199, 1199, 1200, 1201, 1202, 1202, 1203, 1204, 1205, 1205, 1206, 1207, 1208, 1208, 1209, 1210, 1210, \
    1211, 1212, 1213, 1213, 1214, 1215, 1216, 1216, 1217, 1218, 1219, 1219, 1220, 1221, 1221, 1222, 1223, 1224, 1224, 1225, \
    1226, 1227, 1227, 1228, 1229, 1229, 1230, 1231, 1232, 1232, 1233, 1234, 1234, 1235, 1236, 1237, 1237, 1238, 1239, 1239, \
    1240, 1241, 1241, 1242, 1243, 1244, 1244, 1245, 1246, 1246, 1247, 1248, 1248, 1249, 1250, 1251, 1251, 1252, 1253, 1253, \
    1254, 1255, 1255, 1256, 1257, 1257, 1258, 1259, 1260, 1260, 1261, 1262, 1262, 1263, 1264, 1264, 1265, 1266, 1266, 1267, \
    1268, 1268, 1269, 1270, 1270, 1271, 1272, 1272, 1273, 1274, 1275, 1275, 1276, 1277, 1277, 1278, 1279, 1279, 1280, 1281, \
    1281, 1282, 1283, 1283, 1284, 1285, 1285, 1286, 1287, 1287, 1288, 1289, 1289, 1290, 1290, 1291, 1292, 1292, 1293, 1294, \
    1294, 1295, 1296, 1296, 1297, 1298, 1298, 1299, 1300, 1300, 1301, 1302, 1302, 1303, 1304, 1304, 1305, 1306, 1306, 1307, \
    1307, 1308, 1309, 1309, 1310, 1311, 1311, 1312, 1313, 1313, 1314, 1315, 1315, 1316, 1316, 1317, 1318, 1318, 1319, 1320, \
    1320, 1321, 1322, 1322, 1323, 1323, 1324, 1325, 1325, 1326, 1327, 1327, 1328, 1329, 1329, 1330, 1330, 1331, 1332, 1332, \
    1333, 1334, 1334, 1335, 1335, 1336, 1337, 1337, 1338, 1339, 1339, 1340, 1340, 1341, 1342, 1342, 1343, 1344, 1344, 1345, \
    1345, 1346, 1347, 1347, 1348, 1348, 1349, 1350, 1350, 1351, 1352, 1352, 1353, 1353, 1354, 1355, 1355, 1356, 1356, 1357, \
    1358, 1358, 1359, 1359, 1360, 1361, 1361, 1362, 1363, 1363, 1364, 1364, 1365, 1366, 1366, 1367, 1367, 1368, 1369, 1369, \
    1370, 1370, 1371, 1372, 1372, 1373, 1373, 1374, 1375, 1375, 1376, 1376, 1377, 1378, 1378, 1379, 1379, 1380, 1381, 1381, \
    1382, 1382, 1383, 1384, 1384, 1385, 1385, 1386, 1387, 1387, 1388, 1388, 1389, 1389, 1390, 1391, 1391, 1392, 1392, 1393, \
    1394, 1394, 1395, 1395, 1396, 1397, 1397, 1398, 1398, 1399, 1399, 1400, 1401, 1401, 1402, 1402, 1403, 1404, 1404, 1405, \
    1405, 1406, 1406, 1407, 1408, 1408, 1409, 1409, 1410, 1410, 1411, 1412, 1412, 1413, 1413, 1414, 1415, 1415, 1416, 1416, \
    1417, 1417, 1418, 1419, 1419, 1420, 1420, 1421, 1421, 1422, 1423, 1423, 1424, 1424, 1425, 1425, 1426, 1427, 1427, 1428, \
    1428, 1429, 1429, 1430, 1431, 1431, 1432, 1432, 1433, 1433, 1434, 1435, 1435, 1436, 1436, 1437, 1437, 1438, 1438, 1439, \
    1440, 1440, 1441, 1441, 1442, 1442, 1443, 1444, 1444, 1445, 1445, 1446, 1446, 1447, 1447, 1448, 1449, 1449, 1450, 1450, \
    1451, 1451, 1452, 1452, 1453, 1454, 1454, 1455, 1455, 1456, 1456, 1457, 1457, 1458, 1459, 1459, 1460, 1460, 1461, 1461, \
    1462, 1462, 1463, 1464, 1464, 1465, 1465, 1466, 1466, 1467, 1467, 1468, 1469, 1469, 1470, 1470, 1471, 1471, 1472, 1472, \
    1473, 1473, 1474, 1475, 1475, 1476, 1476, 1477, 1477, 1478, 1478, 1479, 1479, 1480, 1481, 1481, 1482, 1482, 1483, 1483, \
    1484, 1484, 1485, 1485, 1486, 1487, 1487, 1488, 1488, 1489, 1489, 1490, 1490, 1491, 1491, 1492, 1492, 1493, 1494, 1494, \
    1495, 1495, 1496, 1496, 1497, 1497, 1498, 1498, 1499, 1499, 1500, 1501, 1501, 1502, 1502, 1503, 1503, 1504, 1504, 1505, \
    1505, 1506, 1506, 1507, 1508, 1508, 1509, 1509, 1510, 1510, 1511, 1511, 1512, 1512, 1513, 1513, 1514, 1514, 1515, 1515, \
    1516, 1517, 1517, 1518, 1518, 1519, 1519, 1520, 1520, 1521, 1521, 1522, 1522, 1523, 1523, 1524, 1524, 1525, 1526, 1526, \
    1527, 1527, 1528, 1528, 1529, 1529, 1530, 1530, 1531, 1531, 1532, 1532, 1533, 1533, 1534, 1534, 1535, 1535, 1536, 1537, \
    1537, 1538, 1538, 1539, 1539, 1540, 1540, 1541, 1541, 1542, 1542, 1543, 1543, 1544, 1544, 1545, 1545, 1546, 1546, 1547, \
    1547, 1548, 1549, 1549, 1550, 1550, 1551, 1551, 1552, 1552, 1553, 1553, 1554, 1554, 1555, 1555, 1556, 1556, 1557, 1557, \
    1558, 1558, 1559, 1559, 1560, 1560, 1561, 1561, 1562, 1562, 1563, 1563, 1564, 1565, 1565, 1566, 1566, 1567, 1567, 1568, \
    1568, 1569, 1569, 1570, 1570, 1571, 1571, 1572, 1572, 1573, 1573, 1574, 1574, 1575, 1575, 1576, 1576, 1577, 1577, 1578, \
    1578, 1579, 1579, 1580, 1580, 1581, 1581, 1582, 1582, 1583, 1583, 1584, 1584, 1585, 1585, 1586, 1586, 1587, 1587, 1588, \
    1588, 1589, 1589, 1590, 1590, 1591, 1591, 1592, 1593, 1593, 1594, 1594, 1595, 1595, 1596, 1596, 1597, 1597, 1598, 1598, \
    1599, 1599, 1600, 1600, 1601, 1601, 1602, 1602, 1603, 1603, 1604, 1604, 1605, 1605, 1606, 1606, 1607, 1607, 1608, 1608, \
    1609, 1609, 1610, 1610, 1611, 1611, 1612, 1612, 1613, 1613, 1614, 1614, 1615, 1615, 1616, 1616, 1617, 1617, 1618, 1618, \
    1619, 1619, 1620, 1620, 1621, 1621, 1622, 1622, 1623, 1623, 1624, 1624, 1625, 1625, 1626, 1626, 1627, 1627, 1627, 1628, \
    1628, 1629, 1629, 1630, 1630, 1631, 1631, 1632, 1632, 1633, 1633, 1634, 1634, 1635, 1635, 1636, 1636, 1637, 1637, 1638, \
    1638, 1639, 1639, 1640, 1640, 1641, 1641, 1642, 1642, 1643, 1643, 1644, 1644, 1645, 1645, 1646, 1646, 1647, 1647, 1648, \
    1648, 1649, 1649, 1650, 1650, 1651, 1651, 1652, 1652, 1653, 1653, 1654, 1654, 1655, 1655, 1656, 1656, 1657, 1657, 1657, \
    1658, 1658, 1659, 1659, 1660, 1660, 1661, 1661, 1662, 1662, 1663, 1663, 1664, 1664, 1665, 1665, 1666, 1666, 1667, 1667, \
    1668, 1668, 1669, 1669, 1670, 1670, 1671, 1671, 1672, 1672, 1673, 1673, 1674, 1674, 1674, 1675, 1675, 1676, 1676, 1677, \
    1677, 1678, 1678, 1679, 1679, 1680, 1680, 1681, 1681, 1682, 1682, 1683, 1683, 1684, 1684, 1685, 1685, 1686, 1686, 1687, \
    1687, 1688, 1688, 1688, 1689, 1689, 1690, 1690, 1691, 1691, 1692, 1692, 1693, 1693, 1694, 1694, 1695, 1695, 1696, 1696, \
    1697, 1697, 1698, 1698, 1699, 1699, 1700, 1700, 1700, 1701, 1701, 1702, 1702, 1703, 1703, 1704, 1704, 1705, 1705, 1706, \
    1706, 1707, 1707, 1708, 1708, 1709, 1709, 1710, 1710, 1711, 1711, 1711, 1712, 1712, 1713, 1713, 1714, 1714, 1715, 1715, \
    1716, 1716, 1717, 1717, 1718, 1718, 1719, 1719, 1720, 1720, 1721, 1721, 1721, 1722, 1722, 1723, 1723, 1724, 1724, 1725, \
    1725, 1726, 1726, 1727, 1727, 1728, 1728, 1729, 1729, 1730, 1730, 1730, 1731, 1731, 1732, 1732, 1733, 1733, 1734, 1734, \
    1735, 1735, 1736, 1736, 1737, 1737, 1738, 1738, 1738, 1739, 1739, 1740, 1740, 1741, 1741, 1742, 1742, 1743, 1743, 1744, \
    1744, 1745, 1745, 1746, 1746, 1747, 1747, 1747, 1748, 1748, 1749, 1749, 1750, 1750, 1751, 1751, 1752, 1752, 1753, 1753, \
    1754, 1754, 1754, 1755, 1755, 1756, 1756, 1757, 1757, 1758, 1758, 1759, 1759, 1760, 1760, 1761, 1761, 1762, 1762, 1762, \
    1763, 1763, 1764, 1764, 1765, 1765, 1766, 1766, 1767, 1767, 1768, 1768, 1769, 1769, 1769, 1770, 1770, 1771, 1771, 1772, \
    1772, 1773, 1773, 1774, 1774, 1775, 1775, 1776, 1776, 1776, 1777, 1777, 1778, 1778, 1779, 1779, 1780, 1780, 1781, 1781, \
    1782, 1782, 1782, 1783, 1783, 1784, 1784, 1785, 1785, 1786, 1786, 1787, 1787, 1788, 1788, 1789, 1789, 1789, 1790, 1790, \
    1791, 1791, 1792, 1792, 1793, 1793, 1794, 1794, 1795, 1795, 1795, 1796, 1796, 1797, 1797, 1798, 1798, 1799, 1799, 1800, \
    1800, 1801, 1801, 1801, 1802, 1802, 1803, 1803, 1804, 1804, 1805, 1805, 1806, 1806, 1807, 1807, 1807, 1808, 1808, 1809, \
    1809, 1810, 1810, 1811, 1811, 1812, 1812, 1813, 1813, 1813, 1814, 1814, 1815, 1815, 1816, 1816, 1817, 1817, 1818, 1818, \
    1819, 1819, 1819, 1820, 1820, 1821, 1821, 1822, 1822, 1823, 1823, 1824, 1824, 1824, 1825, 1825, 1826, 1826, 1827, 1827, \
    1828, 1828, 1829, 1829, 1830, 1830, 1830, 1831, 1831, 1832, 1832, 1833, 1833, 1834, 1834, 1835, 1835, 1835, 1836, 1836, \
    1837, 1837, 1838, 1838, 1839, 1839, 1840, 1840, 1841, 1841, 1841, 1842, 1842, 1843, 1843, 1844, 1844, 1845, 1845, 1846, \
    1846, 1846, 1847, 1847, 1848, 1848, 1849, 1849, 1850, 1850, 1851, 1851, 1851, 1852, 1852, 1853, 1853, 1854, 1854, 1855, \
    1855, 1856, 1856, 1856, 1857, 1857, 1858, 1858, 1859, 1859, 1860, 1860, 1861, 1861, 1861, 1862, 1862, 1863, 1863, 1864, \
    1864, 1865, 1865, 1866, 1866, 1866, 1867, 1867, 1868, 1868, 1869, 1869, 1870, 1870, 1871, 1871, 1871, 1872, 1872, 1873, \
    1873, 1874, 1874, 1875, 1875, 1876, 1876, 1876, 1877, 1877, 1878, 1878, 1879, 1879, 1880, 1880, 1881, 1881, 1881, 1882, \
    1882, 1883, 1883, 1884, 1884, 1885, 1885, 1885, 1886, 1886, 1887, 1887, 1888, 1888, 1889, 1889, 1890, 1890, 1890, 1891, \
    1891, 1892, 1892, 1893, 1893, 1894, 1894, 1895, 1895, 1895, 1896, 1896, 1897, 1897, 1898, 1898, 1899, 1899, 1899, 1900, \
    1900, 1901, 1901, 1902, 1902, 1903, 1903, 1904, 1904, 1904, 1905, 1905, 1906, 1906, 1907, 1907, 1908, 1908, 1908, 1909, \
    1909, 1910, 1910, 1911, 1911, 1912, 1912, 1912, 1913, 1913, 1914, 1914, 1915, 1915, 1916, 1916, 1917, 1917, 1917, 1918, \
    1918, 1919, 1919, 1920, 1920, 1921, 1921, 1921, 1922, 1922, 1923, 1923, 1924, 1924, 1925, 1925, 1926, 1926, 1926, 1927, \
    1927, 1928, 1928, 1929, 1929, 1930, 1930, 1930, 1931, 1931, 1932, 1932, 1933, 1933, 1934, 1934, 1934, 1935, 1935, 1936, \
    1936, 1937, 1937, 1938, 1938, 1938, 1939, 1939, 1940, 1940, 1941, 1941, 1942, 1942, 1942, 1943, 1943, 1944, 1944, 1945, \
    1945, 1946, 1946, 1947, 1947, 1947, 1948, 1948, 1949, 1949, 1950, 1950, 1951, 1951, 1951, 1952, 1952, 1953, 1953, 1954, \
    1954, 1955, 1955, 1955, 1956, 1956, 1957, 1957, 1958, 1958, 1959, 1959, 1959, 1960, 1960, 1961, 1961, 1962, 1962, 1963, \
    1963, 1963, 1964, 1964, 1965, 1965, 1966, 1966, 1967, 1967, 1967, 1968, 1968, 1969, 1969, 1970, 1970, 1971, 1971, 1971, \
    1972, 1972, 1973, 1973, 1974, 1974, 1975, 1975, 1975, 1976, 1976, 1977, 1977, 1978, 1978, 1979, 1979, 1979, 1980, 1980, \
    1981, 1981, 1982, 1982, 1983, 1983, 1983, 1984, 1984, 1985, 1985, 1986, 1986, 1986, 1987, 1987, 1988, 1988, 1989, 1989, \
    1990, 1990, 1990, 1991, 1991, 1992, 1992, 1993, 1993, 1994, 1994, 1994, 1995, 1995, 1996, 1996, 1997, 1997, 1998, 1998, \
    1998, 1999, 1999, 2000, 2000, 2001, 2001, 2002, 2002, 2002, 2003, 2003, 2004, 2004, 2005, 2005, 2006, 2006, 2006, 2007, \
    2007, 2008, 2008, 2009, 2009, 2009, 2010, 2010, 2011, 2011, 2012, 2012, 2013, 2013, 2013, 2014, 2014, 2015, 2015, 2016, \
    2016, 2017, 2017, 2017, 2018, 2018, 2019, 2019, 2020, 2020, 2021, 2021, 2021, 2022, 2022, 2023, 2023, 2024, 2024, 2024, \
    2025, 2025, 2026, 2026, 2027, 2027, 2028, 2028, 2028, 2029, 2029, 2030, 2030, 2031, 2031, 2032, 2032, 2032, 2033, 2033, \
    2034, 2034, 2035, 2035, 2035, 2036, 2036, 2037, 2037, 2038, 2038, 2039, 2039, 2039, 2040, 2040, 2041, 2041, 2042, 2042, \
    2042, 2043, 2043, 2044, 2044, 2045, 2046, 2047
};




int lm3697_lcd_backlight_set_level(unsigned int bl_level)
{
        struct lm3697_bl_chip *pchip = lm3697_pchip;
        unsigned int BL_MSB = 0;
        unsigned int BL_LSB = 0;
        int ret = 0;

        if(!pchip || !lm3697_pchip){
            pr_err("%s  lm3697_lcd_backlight_set_level pchip is null.\n", __func__);
            return 0;
            }

        if (!pchip->regmap || !lm3697_pchip->regmap) {
            pr_err("%s  pchip->regmap is NULL.\n", __func__);
            return 0;
        }

        backlight_level =  bl_level;

         #ifdef VENDOR_EDIT
        //YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2017/02/28,
        //add for 2048 bl level too low in ftm mode
        if(LM3697_EXPONENTIAL && LM3697_LEVEL_2048_SUPPROT
            && (get_boot_mode() == MSM_BOOT_MODE__FACTORY)
            && (bl_level != 0)){
            bl_level = 1800;
            pr_err("%s: set bl_level=%d\n", __func__, bl_level);
        }
        #endif /*VENDOR_EDIT*/


        #ifndef VENDOR_EDIT
        //YongPeng.Yi@MultiMedia.Display.LCD.Feature,2016/09/20,
        //remove for 16037 jdi lcd close cabc at bl <=160, keep cabc config as R9K jdi lcd
        //YongPeng.Yi@SWDP.MultiMedia, 2016/09/19,  Add for tuning display when close cabc at bl <=160
        if(is_project(OPPO_16037)){
            if(bl_level <= CABC_DISABLE_LEVEL && pwm_flag==true){
                set_backlight_pwm(0);
            }else if(bl_level > CABC_DISABLE_LEVEL && pwm_flag==false && cabc_mode>0){
                set_backlight_pwm(1);
            }
        }

        if((cabc_mode>=1) && (bl_level <= CABC_DISABLE_LEVEL)&& (bl_level >= LM3697_MIN_BRIGHTNESS)){
            bl_level = LM3697_MIN_BRIGHTNESS+(bl_level-LM3697_MIN_BRIGHTNESS)*100/CABC_DISABLE_LEVEL;
            if(bl_level <= LM3697_MIN_BRIGHTNESS)
                bl_level = LM3697_MIN_BRIGHTNESS;
           // pr_err("%s: set bl_level=%d\n", __func__, bl_level);
        }
        #endif /*VENDOR_EDIT*/

        //YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/10/14,
        //add for lm3697 bl change to exponential mode
        if(LM3697_EXPONENTIAL){
            BL_MSB = (backlight_buf[bl_level] >>3) & 0xFF;
            BL_LSB = backlight_buf[bl_level] & 0x07;
        }else{
            BL_MSB = (bl_level >>3) & 0xFF;
            BL_LSB = bl_level & 0x07;
        }
        /* brightness 0 means disable */
        if (!bl_level) {
            ret = regmap_write(pchip->regmap, 0x20, BL_LSB);
            if (ret < 0)
                goto out;
            ret = regmap_write(pchip->regmap, 0x21, BL_MSB);
            if (ret < 0)
                goto out;
         }else{
            if(LM3697_LEVEL_2048_SUPPROT){
            ret = regmap_write(pchip->regmap, 0x24, 01);
                if (ret < 0)
                    goto out;
            ret = regmap_write(pchip->regmap, 0x20, BL_LSB);
                    if (ret < 0)
                        goto out;

            ret = regmap_write(pchip->regmap, 0x21, BL_MSB);
                    if (ret < 0)
                        goto out;
            }else{
            ret = regmap_write(pchip->regmap, 0x24, 01);
                if (ret < 0)
                    goto out;

            ret = regmap_write(pchip->regmap, 0x20, 00);
                if (ret < 0)
                    goto out;

            ret = regmap_write(pchip->regmap, 0x21, bl_level);
                if (ret < 0)
                    goto out;
            }
         }

         return ret;
    out:
        pr_err("%s  set lcd backlight failed.\n", __func__);
        return ret;
}

EXPORT_SYMBOL(lm3697_lcd_backlight_set_level);


#ifdef VENDOR_EDIT
/*Ling.Guo@Swdp.MultiMedia.Display, 2017/04/28,modify for high brightness mode */
extern unsigned int current_brightness;
int set_outdoor_brightness(unsigned int brightness,unsigned long outdoor_mode){
    unsigned int outdoor_brightness = brightness;
    unsigned int indoor_brightness = (brightness*93)/100;
    unsigned int save_brightness = current_brightness;
    unsigned int change_slow = 0;
    if((current_brightness - indoor_brightness) < 100){
        change_slow = 1;
    }
    if(outdoor_brightness > 2047){
        outdoor_brightness = 2047;
    }
    if(outdoor_mode == 0){
        while(outdoor_brightness > indoor_brightness){
            outdoor_brightness--;
            if(current_brightness == 0 || save_brightness != current_brightness){
                break;
            }
            lm3697_lcd_backlight_set_level(outdoor_brightness);
            if(change_slow == 1){
                mdelay(10);
            }else{
                mdelay(5);
            }
        }
    }else if(outdoor_mode == 1){
        while(indoor_brightness < outdoor_brightness){
            indoor_brightness++;
            if(current_brightness == 0 || save_brightness != current_brightness){
                break;
            }
            lm3697_lcd_backlight_set_level(indoor_brightness);
            mdelay(4);
        }
    }
    return 1;
}
#endif

static int lm3697_dt(struct device *dev, struct lm3697_platform_data *pdata)
{
    struct device_node *np = dev->of_node;

    pdata->en_gpio = of_get_named_gpio(np, "ti,bl-enable-gpio", 0);

    pr_err("%s bl_en_gpio=%d\n", __func__, pdata->en_gpio);

    if (!gpio_is_valid(pdata->en_gpio))
            pr_err("%s:%d, Backlight_en gpio not specified\n", __func__, __LINE__);

    return 0;
}

#ifdef VENDOR_EDIT
//YongPeng.Yi@SWDP.MultiMedia, 2016/08/20,  Add for lm3697 power set
//YongPeng.Yi@MultiMedia.Display.LCD.Stability,2016/09/21,
//add for lm3697 reg init
int lm3697_reg_init(void){
    struct lm3697_bl_chip *pchip = lm3697_pchip;
    int ret =0;

    if(!pchip || !lm3697_pchip){
        dev_err(pchip->dev, "lm3697_reg_init pdata is null\n");
        return 0;
    }

    if (!pchip->regmap || !lm3697_pchip->regmap || !lm3697_pchip->pdata) {
        pr_err("%s  pchip->regmap is NULL.\n", __func__);
        return 0;
    }

    pr_debug("%s: init lm3697 reg\n", __func__);

    ret = regmap_write(pchip->regmap, 0x10, 0x00);  //HVLED1, 2, 3 enable
        if (ret < 0)
         goto out;
    ret = regmap_write(pchip->regmap, 0x1A, 0x05);  //OVP 32V, Freq 1M
        if (ret < 0)
         goto out;
    //YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/10/14,
    //add for lm3697 bl change to exponential mode
    if(LM3697_EXPONENTIAL){
        pr_debug("%s lm3697 Set Exponential Mapping Mode.\n", __func__);
        ret = regmap_write(pchip->regmap, 0x16, 0x00);  //Exponential Mapping Mode
            if (ret < 0)
             goto out;
    }else{
        ret = regmap_write(pchip->regmap, 0x16, 0x01);  //Linear Mapping Mode
            if (ret < 0)
             goto out;
    }
    ret = regmap_write(pchip->regmap, 0x17, 0x11);  //18mA
        if (ret < 0)
         goto out;
    ret = regmap_write(pchip->regmap, 0x18, 0x11);  //18mA
        if (ret < 0)
         goto out;
    ret = regmap_write(pchip->regmap, 0x19, 0x07);  //Linear Mapping Mode
        if (ret < 0)
         goto out;
    ret = regmap_write(pchip->regmap, 0x1C, 0x0D);  //set pwm on
        if (ret < 0)
         goto out;
    return ret;
out:
    dev_err(pchip->dev, "i2c failed to access register\n");
    return ret;
}
void lm3697_bl_enable(int enable){
    struct lm3697_bl_chip *pchip = lm3697_pchip;

    if(!pchip || !lm3697_pchip){
        pr_err("%s  lm3697_bl_enable pdata is null.\n", __func__);
        return;
    }
    //YongPeng.Yi@SWDP.MultiMedia, 2016/08/22,  Add for lm3697 init failed to null check
    if (!pchip->regmap || !lm3697_pchip->regmap || !lm3697_pchip->pdata) {
        pr_err("%s  pchip->regmap is NULL.\n", __func__);
        return;
    }

    pr_info("mdss %s  = %d\n", __func__, enable);

    if(enable){
        if (gpio_is_valid(pchip->pdata->en_gpio)){
            gpio_set_value((pchip->pdata->en_gpio), 1);
        }else{
            pr_err("%s: enable failed", __func__);
        }
    }else{
        if (gpio_is_valid(pchip->pdata->en_gpio)){
            gpio_set_value((pchip->pdata->en_gpio), 0);
        }else{
            pr_err("%s: disable failed", __func__);
        }
    }
}
EXPORT_SYMBOL(lm3697_bl_enable);

//YongPeng.Yi@SWDP.MultiMedia, 2016/09/01,  Add for cabc
int set_backlight_pwm(int state)
{
    int rc = 0;
    return rc;
    if(!lm3697_pchip->regmap) {
        pr_err("%s  lm3697_pchip->regmap is NULL.\n", __func__);
        return 0;
    }
    pr_err("%s: state = %d pwm_flag = %d\n", __func__, state, pwm_flag);
    return rc;
}
EXPORT_SYMBOL(set_backlight_pwm);
#endif /*VENDOR_EDIT*/

static int lm3697_chip_init(struct lm3697_bl_chip *pchip){
    int ret = 0;

    ret = regmap_write(pchip->regmap, 0x10, 0x00);  //HVLED1, 2, 3 enable
        if (ret < 0)
        goto out;

    ret = regmap_write(pchip->regmap, 0x1A, 0x05);  //OVP 32V, Freq 1MHz
        if (ret < 0)
        goto out;

    //YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2016/10/14,
    //add for lm3697 bl change to exponential mode
    if(LM3697_EXPONENTIAL){
        pr_debug("%s lm3697 Set Exponential Mapping Mode.\n", __func__);
        ret = regmap_write(pchip->regmap, 0x16, 0x00);  //Exponential Mapping Mode
            if (ret < 0)
             goto out;
    }else{
        ret = regmap_write(pchip->regmap, 0x16, 0x01);  //Linear Mapping Mode
            if (ret < 0)
             goto out;
    }

    ret = regmap_write(pchip->regmap, 0x17, 0x11);  //Bank A Full-scale current (18.6mA)
    if (ret < 0)
        goto out;

    ret = regmap_write(pchip->regmap, 0x18, 0x11);  //Bank A Full-scale current (18.6mA)
    if (ret < 0)
        goto out;


    ret = regmap_write(pchip->regmap, 0x19, 0x07);  //Linear Mapping Mode
    if (ret < 0)
        goto out;

    //YongPeng.Yi@MultiMedia.Display.LCD.Stability,2016/09/21,
    //add for init set pwm on
    ret = regmap_write(pchip->regmap, 0x1C, 0x0D);  //set pwm on
    if (ret < 0)
        goto out;

    //ret = regmap_write(pchip->regmap, 0x17, 0x13);  //Bank A Full-scale current (20.2mA)
    //  if (ret < 0)
    //  goto out;
    //regmap_write(pchip->regmap, 0x18, 0x13);    //Bank B Full-scale current (20.2mA)
    ret = regmap_write(pchip->regmap, 0x24, 0x01);  //Enable Bank A / Disable Bank B
        if (ret < 0)
        goto out;

    return ret;

out:
    dev_err(pchip->dev, "i2c failed to access register\n");
    return ret;
}

static struct regmap_config lm3697_regmap = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = LM3697_MAX_REGISTERS,
};

static int lm3697_bl_probe(struct i2c_client *client,
               const struct i2c_device_id *id)
{
    struct lm3697_platform_data *pdata = client->dev.platform_data;
    struct lm3697_bl_chip *pchip;
    unsigned int revision;
    static char *temp;
    int ret = 0;

    pr_err("%s Enter\n", __func__);

    if (client->dev.of_node) {
        pdata = devm_kzalloc(&client->dev,
            sizeof(struct lm3697_platform_data), GFP_KERNEL);
        if (!pdata) {
            dev_err(&client->dev, "Failed to allocate memory\n");
            return -ENOMEM;
        }

        ret = lm3697_dt(&client->dev, pdata);
        if (ret)
            return ret;
    } else
        pdata = client->dev.platform_data;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "fail : i2c functionality check...\n");
        return -EOPNOTSUPP;
    }

    if (pdata == NULL) {
        dev_err(&client->dev, "fail : no platform data.\n");
        return -ENODATA;
    }

    pchip = devm_kzalloc(&client->dev, sizeof(struct lm3697_bl_chip),
                 GFP_KERNEL);
    if (!pchip)
        return -ENOMEM;
    lm3697_pchip = pchip;


    pchip->pdata = pdata;
    pchip->dev = &client->dev;

    ret = gpio_request(pdata->en_gpio, "backlight_enable");
    if (ret) {
        pr_err("request enable gpio failed, ret=%d\n", ret);
    }
    pr_err("%s bl_en_gpio=%d\n", __func__, pdata->en_gpio);

    if (gpio_is_valid(pdata->en_gpio)){
        gpio_set_value((pdata->en_gpio), 1);
        gpio_direction_output((pdata->en_gpio), 1);
    }

    pchip->regmap = devm_regmap_init_i2c(client, &lm3697_regmap);
    if (IS_ERR(pchip->regmap)) {
        ret = PTR_ERR(pchip->regmap);
        dev_err(&client->dev, "fail : allocate register map: %d\n",
            ret);
        return ret;
    }

    i2c_set_clientdata(client, pchip);

    /* chip initialize */
    ret = lm3697_chip_init(pchip);
    if (ret < 0) {
        dev_err(&client->dev, "fail : init chip\n");
        //YongPeng.Yi@MultiMedia.Display.LCD.Stability, 2017/03/08,
        //add for bl key log
        goto error_enable;
    }

    regmap_read(pchip->regmap,0x00,&revision);
    if (revision == 0x02) {
        temp = "02";
    }else if (revision == 0x00) {
        temp = "00";
    }else {
        temp = "unknown";
    }

    pr_err("%s :revision = %s\n", __func__, temp);


    //register_device_proc("backlight", temp, "LM3697");


    pr_info("%s : probe done\n", __func__);

    return 0;


error_enable:
    /* chip->pdata and chip->pdata->bl_pdata
     * are allocated in lm3697_bl_parse_dt() by devm_kzalloc()
     */

    devm_kfree(&client->dev, pchip->pdata);
    devm_kfree(&client->dev, pchip);
    pr_err("%s : probe failed\n", __func__);
    return ret;
}

static int lm3697_bl_remove(struct i2c_client *client){
    struct lm3697_bl_chip *pchip = i2c_get_clientdata(client);
    int ret = 0;

    pr_err("%s :  failed\n", __func__);

    ret = regmap_write(pchip->regmap, LM3697_REG_BRT_A_LSB, 0);
    if (ret < 0)
        dev_err(pchip->dev, "i2c failed to access register\n");

    ret = regmap_write(pchip->regmap, LM3697_REG_BRT_A_MSB, 0);
    if (ret < 0)
        dev_err(pchip->dev, "i2c failed to access register\n");

    if (gpio_is_valid(pchip->pdata->en_gpio)){
        gpio_set_value(pchip->pdata->en_gpio, 0);
        gpio_free(pchip->pdata->en_gpio);
    }

    return 0;
}

static const struct i2c_device_id lm3697_bl_ids[] = {
    { "lm3697", 0 },
    { }
};


static struct i2c_driver lm3697_i2c_driver = {
    .probe = lm3697_bl_probe,
    .remove = lm3697_bl_remove,
    .driver = {
        .name = "lm3697",
        .owner = THIS_MODULE,
    },
    .id_table = lm3697_bl_ids,
};

module_i2c_driver(lm3697_i2c_driver);

MODULE_DESCRIPTION("TI LM3697 Backlight Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
