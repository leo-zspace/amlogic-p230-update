#ifndef DEFS_H
#define DEFS_H

typedef char __int8;
typedef short __int16;
typedef int __int32;
typedef long long __int64;

template <typename T>
inline T const& max (T const& a, T const& b)
{
  return  a < b ? b : a;
}

template <typename T>
inline T const& min (T const& a, T const& b)
{
  return  a > b ? b : a;
}

#define TYPE_AT(type, base, offset) (*(type*)((char*)(base)+(offset)))
#define CHAR_AT(base, offset) (TYPE_AT(char, base, offset))
#define SHORT_AT(base, offset) (le16toh(TYPE_AT(short, base, offset)))
#define INT_AT(base, offset) (le32toh(TYPE_AT(int, base, offset)))
#define LONG_AT(base, offset) (le64toh(TYPE_AT(long long, base, offset)))

#endif //DEFS_H
