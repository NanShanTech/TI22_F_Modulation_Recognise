"""
STM32 Memory Inspector HEX 文件解析器

将 Keil/IAR 等 IDE dump 的 Intel HEX 文件解析为 numpy 数组。

支持的 dtype:
  f32, f64       — float32 / float64 (小端)
  u8, u16, u32   — unsigned integer
  i8, i16, i32   — signed integer (小端)
  b8, b16, b32   — 大端 (big-endian) 整数
  或直接传入 numpy dtype, 如 np.dtype('>f4')

用法:
  from readhex import readhex
  data = readhex('adc_buffer_16384.hex', 'f32')
"""

import numpy as np
from pathlib import Path


_DTYPE_MAP = {
    'f32':    '<f4',
    'float32': '<f4',
    'f64':    '<f8',
    'float64': '<f8',
    'u8':     'u1',
    'uint8':  'u1',
    'u16':    '<u2',
    'uint16': '<u2',
    'u32':    '<u4',
    'uint32': '<u4',
    'i8':     'i1',
    'int8':   'i1',
    'i16':    '<i2',
    'int16':  '<i2',
    'i32':    '<i4',
    'int32':  '<i4',
    'b8':     '>u1',
    'b16':    '>u2',
    'b32':    '>u4',
}


def readhex(filepath: str | Path, dtype: str | np.dtype = 'f32') -> np.ndarray:
    """
    解析 Intel HEX 文件并返回 numpy 数组。

    Parameters
    ----------
    filepath : str | Path
        HEX 文件路径。
    dtype : str | np.dtype
        数据类型。
        - 字符串: 'f32', 'f64', 'u8', 'u16', 'u32', 'i8', 'i16', 'i32',
          'b8', 'b16', 'b32' (b 开头为大端)
        - np.dtype: 任意 numpy dtype

    Returns
    -------
    np.ndarray
        解析后的数据,一维数组。

    Raises
    ------
    ValueError
        文件中无数据记录,或不支持的 dtype。
    """
    raw = _parse_hex(filepath)

    # 解析 dtype
    if isinstance(dtype, str):
        if dtype.lower() not in _DTYPE_MAP:
            raise ValueError(
                f"不支持的 dtype: '{dtype}'\n"
                f"支持的内置类型: {list(_DTYPE_MAP.keys())}"
            )
        dt = np.dtype(_DTYPE_MAP[dtype.lower()])
    else:
        dt = np.dtype(dtype)

    # 检查对齐
    n = len(raw) // dt.itemsize
    if n * dt.itemsize != len(raw):
        print(f"警告: {len(raw)} 字节不能被 {dt.itemsize} 整除,丢弃尾部 {len(raw) % dt.itemsize} 字节")

    return np.frombuffer(raw, dtype=dt, count=n)


def _parse_hex(filepath: str | Path) -> bytes:
    """
    解析 Intel HEX 文件,返回连续的原始字节数据。

    支持多段不连续地址(如 STM32 调试器 dump 跳过的内存空洞),
    自动拼接为最小连续范围。
    """
    chunks: list[tuple[int, bytes]] = []
    base_addr = 0

    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line[0] != ':':
                continue

            byte_count = int(line[1:3], 16)
            address    = int(line[3:7], 16)
            rec_type   = int(line[7:9], 16)

            if rec_type == 0x04:        # Extended Linear Address
                base_addr = int(line[9:13], 16) << 16
            elif rec_type == 0x00:      # Data Record
                payload = bytes.fromhex(line[9:9 + byte_count * 2])
                chunks.append((base_addr + address, payload))
            elif rec_type == 0x01:      # End Of File
                break

    if not chunks:
        raise ValueError("HEX 文件中没有数据记录 (record type 0x00)")

    # 按地址排序,计算连续范围
    chunks.sort(key=lambda x: x[0])
    start = chunks[0][0]
    end   = max(addr + len(payload) for addr, payload in chunks)

    data = bytearray(end - start)
    for addr, payload in chunks:
        offset = addr - start
        data[offset:offset + len(payload)] = payload

    return bytes(data)


# ----- 简单命令行测试 -----
if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print("用法: python readhex.py <hex_file> [dtype]")
        print("示例: python readhex.py adc_buffer_16384.hex f32")
        sys.exit(1)

    fpath = sys.argv[1]
    dt = sys.argv[2] if len(sys.argv) > 2 else 'f32'
    arr = readhex(fpath, dt)

    print(f"文件: {fpath}")
    print(f"dtype: {dt} → {arr.dtype}")
    print(f"形状: {arr.shape}")
    print(f"前 8 个值: {arr[:8]}")
    print(f"统计:  max={arr.max():.6g}  min={arr.min():.6g}  "
          f"mean={arr.mean():.6g}  rms={np.sqrt(np.mean(arr**2)):.6g}")
