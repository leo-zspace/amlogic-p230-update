#pragma once

#define AML_ID_VENDOR  0x1B8E
#define AML_ID_PRODUCE 0xC003

// assuming Windows are running on little endian platform (x86/x64):

#define htobe16(x) _byteswap_ushort(x)
#define htole16(x) (x)
#define be16toh(x) _byteswap_ushort(x)
#define le16toh(x) (x)

#define htobe32(x) _byteswap_ulong(x)
#define htole32(x) (x)
#define be32toh(x) _byteswap_ulong(x)
#define le32toh(x) (x)

#define htobe64(x) _byteswap_uint64(x)
#define htole64(x) (x)
#define be64toh(x) _byteswap_uint64(x)
#define le64toh(x) (x)

#define TYPE_AT(type, base, offset) (*(type*)((char*)(base) + (offset)))

#define CHAR_AT(base, offset) (TYPE_AT(char, base, offset))
#define SHORT_AT(base, offset) (le16toh(TYPE_AT(short, base, offset)))
#define INT_AT(base, offset) (le32toh(TYPE_AT(int, base, offset)))
#define LONG_AT(base, offset) (le64toh(TYPE_AT(long long, base, offset)))

#define SET_CHAR_AT(base, offset, value) (TYPE_AT(char, base, offset) = (value))
#define SET_SHORT_AT(base, offset, value) (TYPE_AT(short, base, offset) = htole16(value))
#define SET_INT_AT(base, offset, value) (TYPE_AT(int, base, offset) = htole32(value))
#define SET_LONG_AT(base, offset, value) (TYPE_AT(long long, base, offset) = htole64(value))


