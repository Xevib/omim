#include "testing/testing.hpp"
#include "coding/byte_stream.hpp"
#include "coding/reader.hpp"
#include "coding/trie.hpp"
#include "coding/trie_builder.hpp"
#include "coding/trie_reader.hpp"
#include "coding/write_to_sink.hpp"

#include "indexer/coding_params.hpp"
#include "indexer/string_file_values.hpp"

#include "base/logging.hpp"

#include "std/algorithm.hpp"
#include "std/cstring.hpp"
#include "std/string.hpp"
#include "std/vector.hpp"

#include <boost/utility/binary.hpp>

namespace
{

struct ChildNodeInfo
{
  bool m_isLeaf;
  uint32_t m_size;
  vector<uint32_t> m_edge;

  ChildNodeInfo(bool isLeaf, uint32_t size, char const * edge) : m_isLeaf(isLeaf), m_size(size)
  {
    while (*edge)
      m_edge.push_back(*edge++);
  }

  uint32_t Size() const { return m_size; }
  bool IsLeaf() const { return m_isLeaf; }
  uint32_t const * GetEdge() const { return &m_edge[0]; }
  uint32_t GetEdgeSize() const { return m_edge.size(); }
};

struct KeyValuePair
{
  buffer_vector<trie::TrieChar, 8> m_key;
  uint32_t m_value;

  KeyValuePair() {}

  template <class TString>
  KeyValuePair(TString const & key, int value)
    : m_key(key.begin(), key.end()), m_value(value)
  {}

  uint32_t GetKeySize() const { return m_key.size(); }
  trie::TrieChar const * GetKeyData() const { return m_key.data(); }
  uint32_t GetValue() const { return m_value; }

  inline void const * value_data() const { return &m_value; }

  inline size_t value_size() const { return sizeof(m_value); }

  bool operator == (KeyValuePair const & p) const
  {
    return (m_key == p.m_key && m_value == p.m_value);
  }

  bool operator < (KeyValuePair const & p) const
  {
    return ((m_key != p.m_key) ? m_key < p.m_key : m_value < p.m_value);
  }

  void Swap(KeyValuePair & r)
  {
    m_key.swap(r.m_key);
    swap(m_value, r.m_value);
  }
};

string DebugPrint(KeyValuePair const & p)
{
  string keyS = ::DebugPrint(p.m_key);
  ostringstream out;
  out << "KVP(" << keyS << ", " << p.m_value << ")";
  return out.str();
}

struct KeyValuePairBackInserter
{
  template <class TString>
  void operator()(TString const & s, uint32_t const & value)
  {
    m_v.push_back(KeyValuePair(s, value));
  }

  vector<KeyValuePair> m_v;
};

struct MaxValueCalc
{
  using ValueType = uint8_t;

  ValueType operator() (void const * p, uint32_t size) const
  {
    ASSERT_EQUAL(size, 4, ());
    uint32_t value;
    memcpy(&value, p, 4);
    ASSERT_LESS(value, 256, ());
    return static_cast<uint8_t>(value);
  }
};

class CharValueList
{
public:
  using TValue = char;

  void Init(vector<TValue> const &) {}

  CharValueList(const string & s) : m_string(s) {}

  size_t Size() const { return m_string.size(); }

  bool IsEmpty() const { return m_string.empty(); }

  template <typename TSink>
  void Serialize(TSink & sink) const
  {
    sink.Write(m_string.data(), m_string.size());
  }

private:
  string m_string;
};

}  //  namespace

template <>
class ValueList<uint32_t>
{
public:
  using TValue = uint32_t;

  ValueList() = default;
  ValueList(serial::CodingParams const & codingParams) : m_codingParams(codingParams) {}

  void Init(vector<TValue> const & values) { m_values = values; }

  size_t Size() const { return m_values.size(); }

  bool IsEmpty() const { return m_values.empty(); }

  template <typename TSink>
  void Serialize(TSink & sink) const
  {
    for (auto const & value : m_values)
      WriteToSink(sink, value);
  }

  template <typename TSource>
  void Deserialize(TSource & src, uint32_t valueCount)
  {
    m_values.resize(valueCount);
    for (size_t i = 0; i < valueCount; ++i)
      m_values[i] = ReadPrimitiveFromSource<TValue>(src);
  }

  template <typename TSource>
  void Deserialize(TSource & src)
  {
    while (src.Size() > 0)
    {
      m_values.push_back(TValue());
      m_values.back() = ReadPrimitiveFromSource<TValue>(src);
    }
  }

  template <typename TF>
  void ForEach(TF && f) const
  {
    for (auto const & value : m_values)
      f(value);
  }

  void SetCodingParams(serial::CodingParams const & codingParams) { m_codingParams = codingParams; }

private:
  vector<TValue> m_values;
  serial::CodingParams m_codingParams;
};

#define ZENC bits::ZigZagEncode
#define MKSC(x) static_cast<signed char>(x)
#define MKUC(x) static_cast<uint8_t>(x)

UNIT_TEST(TrieBuilder_WriteNode_Smoke)
{
  vector<uint8_t> buf;
  PushBackByteSink<vector<uint8_t>> sink(buf);
  ChildNodeInfo children[] = {
      ChildNodeInfo(true, 1, "1A"), ChildNodeInfo(false, 2, "B"), ChildNodeInfo(false, 3, "zz"),
      ChildNodeInfo(true, 4,
                    "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"),
      ChildNodeInfo(true, 5, "a")};

  CharValueList valueList("123");
  trie::WriteNode(sink, 0, valueList, &children[0], &children[0] + ARRAY_SIZE(children));
  uint8_t const expected [] =
  {
    BOOST_BINARY(11000101),                                                 // Header: [0b11] [0b000101]
    3,                                                                      // Number of values
    '1', '2', '3',                                                          // Values
    BOOST_BINARY(10000001),                                                 // Child 1: header: [+leaf] [-supershort]  [2 symbols]
    MKUC(ZENC(MKSC('1'))), MKUC(ZENC(MKSC('A') - MKSC('1'))),               // Child 1: edge
    1,                                                                      // Child 1: size
    MKUC(64 | ZENC(MKSC('B') - MKSC('1'))),                                 // Child 2: header: [-leaf] [+supershort]
    2,                                                                      // Child 2: size
    BOOST_BINARY(00000001),                                                 // Child 3: header: [-leaf] [-supershort]  [2 symbols]
    MKUC(ZENC(MKSC('z') - MKSC('B'))), 0,                                   // Child 3: edge
    3,                                                                      // Child 3: size
    BOOST_BINARY(10111111),                                                 // Child 4: header: [+leaf] [-supershort]  [>= 63 symbols]
    69,                                                                     // Child 4: edgeSize - 1
    MKUC(ZENC(MKSC('a') - MKSC('z'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    MKUC(ZENC(MKSC('a') - MKSC('j'))), 2,2,2,2,2,2,2,2,2,                   // Child 4: edge
    4,                                                                      // Child 4: size
    MKUC(BOOST_BINARY(11000000) | ZENC(0)),                                 // Child 5: header: [+leaf] [+supershort]
  };

  TEST_EQUAL(buf, vector<uint8_t>(&expected[0], &expected[0] + ARRAY_SIZE(expected)), ());
}

UNIT_TEST(TrieBuilder_Build)
{
  int const base = 3;
  int const maxLen = 3;

  vector<string> possibleStrings(1, string());
  for (int len = 1; len <= maxLen; ++len)
  {
    for (int i = 0, p = static_cast<int>(pow((double) base, len)); i < p; ++i)
    {
      string s(len, 'A');
      int t = i;
      for (int l = len - 1; l >= 0; --l, t /= base)
        s[l] += (t % base);
      possibleStrings.push_back(s);
    }
  }
  sort(possibleStrings.begin(), possibleStrings.end());
  // LOG(LINFO, (possibleStrings));

  int const count = static_cast<int>(possibleStrings.size());
  for (int i0 = -1; i0 < count; ++i0)
    for (int i1 = i0; i1 < count; ++i1)
      for (int i2 = i1; i2 < count; ++i2)
  {
    vector<KeyValuePair> v;
    if (i0 >= 0) v.push_back(KeyValuePair(possibleStrings[i0], i0));
    if (i1 >= 0) v.push_back(KeyValuePair(possibleStrings[i1], i1 + 10));
    if (i2 >= 0) v.push_back(KeyValuePair(possibleStrings[i2], i2 + 100));
    vector<string> vs;
    for (size_t i = 0; i < v.size(); ++i)
      vs.push_back(string(v[i].m_key.begin(), v[i].m_key.end()));

    vector<uint8_t> buf;
    PushBackByteSink<vector<uint8_t>> sink(buf);
    trie::Build<PushBackByteSink<vector<uint8_t>>, typename vector<KeyValuePair>::iterator,
                ValueList<uint32_t>>(sink, v.begin(), v.end());
    reverse(buf.begin(), buf.end());

    MemReader memReader = MemReader(&buf[0], buf.size());
    auto const root =
        trie::ReadTrie<MemReader, ValueList<uint32_t>>(memReader, serial::CodingParams());
    vector<KeyValuePair> res;
    KeyValuePairBackInserter f;
    trie::ForEachRefWithValues(*root, f, vector<trie::TrieChar>());
    sort(f.m_v.begin(), f.m_v.end());
    TEST_EQUAL(v, f.m_v, ());
  }
}
