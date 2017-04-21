#include "testing/testing.hpp"

#include "search/search_quality/sample.hpp"

#include "base/logging.hpp"

#include <algorithm>
#include <vector>

using namespace search;
using namespace std;

namespace
{
class SampleTest
{
public:
  SampleTest() { Init(); }

protected:
  void Init();

  Sample m_cuba;
  Sample m_riga;
};

void SampleTest::Init()
{
  m_cuba.m_query = strings::MakeUniString("cuba");
  m_cuba.m_locale = "en";
  m_cuba.m_pos = {37.618706, 99.53730574302003};
  m_cuba.m_viewport = {37.1336, 67.1349, 38.0314, 67.7348};
  search::Sample::Result cubaRes;
  cubaRes.m_name = strings::MakeUniString("Cuba");
  cubaRes.m_relevance = search::Sample::Result::Relevance::Relevant;
  cubaRes.m_types.push_back("place-country");
  cubaRes.m_pos = {-80.832886, 15.521132748163712};
  cubaRes.m_houseNumber = "";
  m_cuba.m_results = {cubaRes};

  m_riga.m_query = strings::MakeUniString("riga");
  m_riga.m_locale = "en";
  m_riga.m_pos = {37.65376, 98.51110651930014};
  m_riga.m_viewport = {37.5064, 67.0476, 37.7799, 67.304};
  search::Sample::Result rigaRes;
  rigaRes.m_name = strings::MakeUniString("Rīga");
  rigaRes.m_relevance = search::Sample::Result::Relevance::Vital;
  rigaRes.m_types.push_back("place-city-capital-2");
  rigaRes.m_pos = {24.105186, 107.7819569220319};
  rigaRes.m_houseNumber = "";
  m_riga.m_results = {rigaRes, rigaRes};
}
}  // namespace

UNIT_CLASS_TEST(SampleTest, Smoke)
{
  auto const jsonStr = R"EOF(
  {
    "query": "cuba",
    "locale": "en",
    "position": {
      "x": 37.618706,
      "y": 99.53730574302003
    },
    "viewport": {
      "minx": 37.1336,
      "miny": 67.1349,
      "maxx": 38.0314,
      "maxy": 67.7348
    },
    "results": [
      {
        "name": "Cuba",
        "relevancy": "relevant",
        "types": [
          "place-country"
        ],
        "position": {
          "x": -80.832886,
          "y": 15.521132748163712
        },
        "houseNumber": ""
      }
    ]
  }
  )EOF";

  Sample s;
  TEST(s.DeserializeFromJSON(jsonStr), ());
  TEST_EQUAL(s, m_cuba, ());
}

UNIT_CLASS_TEST(SampleTest, BadViewport)
{
  auto const jsonStr = R"EOF(
  {
    "results": [
      {
        "houseNumber": "",
        "position": {
          "y": 15.521132748163712,
          "x": -80.832886
        },
        "types": [
          "place-country"
        ],
        "relevancy": "relevant",
        "name": "Cuba"
      }
    ],
    "viewport": {
      "maxy": 67.7348,
      "maxx": 38.0314,
    },
    "position": {
      "y": 99.53730574302003,
      "x": 37.618706
    },
    "locale": "en",
    "query": "cuba"
  }
  )EOF";

  Sample s;
  TEST(!s.DeserializeFromJSON(jsonStr), ());
}

UNIT_CLASS_TEST(SampleTest, Arrays)
{
  string lines;
  lines.append(
      R"({"query": "cuba", "locale": "en", "position": {"x": 37.618706, "y": 99.53730574302003}, "viewport": {"minx": 37.1336, "miny": 67.1349, "maxx": 38.0314, "maxy": 67.7348}, "results": [{"name": "Cuba", "relevancy": "relevant", "types": ["place-country"], "position": {"x": -80.832886, "y": 15.521132748163712}, "houseNumber": ""}]})");
  lines.append("\n");

  lines.append(
      R"({"query": "riga", "locale": "en", "position": {"x": 37.65376, "y": 98.51110651930014}, "viewport": {"minx": 37.5064, "miny": 67.0476, "maxx": 37.7799, "maxy": 67.304}, "results": [{"name": "R\u012bga", "relevancy": "vital", "types": ["place-city-capital-2"], "position": {"x": 24.105186, "y": 107.7819569220319}, "houseNumber": ""}, {"name": "R\u012bga", "relevancy": "vital", "types": ["place-city-capital-2"], "position": {"x": 24.105186, "y": 107.7819569220319}, "houseNumber": ""}]})");
  lines.append("\n");

  vector<Sample> samples;
  TEST(Sample::DeserializeFromJSONLines(lines, samples), ());

  vector<Sample> expected = {m_cuba, m_riga};

  sort(samples.begin(), samples.end());
  sort(expected.begin(), expected.end());

  TEST_EQUAL(samples, expected, ());
}

UNIT_CLASS_TEST(SampleTest, SerDes)
{
  vector<Sample> expected = {m_cuba, m_riga};

  std::string lines;
  Sample::SerializeToJSONLines(expected, lines);

  vector<Sample> actual;
  TEST(Sample::DeserializeFromJSONLines(lines, actual), ());

  sort(expected.begin(), expected.end());
  sort(actual.begin(), actual.end());
  TEST_EQUAL(expected, actual, ());
}
