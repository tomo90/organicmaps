#include "search/ranking_info.hpp"

#include "search/utils.hpp"

#include "indexer/classificator.hpp"
#include "indexer/search_string_utils.hpp"

#include "base/assert.hpp"
#include "base/stl_helpers.hpp"

#include <array>
#include <limits>
#include <sstream>

namespace search
{
using namespace std;

namespace
{
// See search/search_quality/scoring_model.py for details.  In short,
// these coeffs correspond to coeffs in a linear model.
double constexpr kHasName = 0.5;
double constexpr kCategoriesPopularity = 0.05;
double constexpr kCategoriesDistanceToPivot = -0.6874177;
double constexpr kCategoriesRank = 1.0000000;
double constexpr kCategoriesFalseCats = -1.0000000;

double constexpr kDistanceToPivot = -0.2123693;
// This constant is very important and checked in Famous_Cities_Rank test.
double constexpr kRank = 0.23;
double constexpr kPopularity = 1.0000000;

// Decreased this value:
// - On the one hand, when search for "eat", we'd prefer category types, rather than "eat" name.
// - On the other hand, when search for "subway", do we usually prefer famous fast food or metro?
double constexpr kFalseCats = -0.01;

double constexpr kErrorsMade = -0.15;
double constexpr kMatchedFraction = 0.1876736;
double constexpr kAllTokensUsed = 0.0478513;
double constexpr kCommonTokens = -0.05;

double constexpr kNameScore[] = {
 -0.05,   // Zero
  0,      // Substring
  0.01,   // Prefix
  0.012,  // First Match
  0.018,  // Full Prefix
  0.02,   // Full Match
};
static_assert(std::size(kNameScore) == static_cast<size_t>(NameScore::COUNT));

// 0-based factors from POIs, Streets, Buildings, since we don't have ratings or popularities now.
double constexpr kType[] = {
  0,          // POI
  0,          // Complex POI
  0.007,      // Building, to compensate max(kStreetType), see Arbat_Address test.
  0,          // Street
  0,          // Suburb
 -0.02,       // Unclassified
  0,          // Village
  0.01,       // City
  0.0233254,  // State
  0.1679389,  // Country
};
static_assert(std::size(kType) == static_cast<size_t>(Model::TYPE_COUNT));

// 0-based factors from General.
double constexpr kPoiType[] = {
  0.03,       // TransportMajor
  0.003,      // TransportLocal (0 < it < Residential st)
  0.01,       // Eat
  0.01,       // Hotel
  0.01,       // Shop or Amenity
  0.01,       // Attraction
 -0.01,       // Service
  0,          // General
};
static_assert(std::size(kPoiType) == base::Underlying(PoiType::Count));

double constexpr kStreetType[] = {
  0,          // Default
  0,          // Pedestrian
  0,          // Cycleway
  0,          // Outdoor
  0.004,      // Minors
  0.004,      // Residential
  0.005,      // Regular
  0.006,      // Motorway
};
static_assert(std::size(kStreetType) == base::Underlying(StreetType::Count));

static_assert(kType[Model::TYPE_BUILDING] > kStreetType[StreetType::Motorway]);

// Coeffs sanity checks.
static_assert(kHasName >= 0, "");
static_assert(kCategoriesPopularity >= 0, "");
static_assert(kDistanceToPivot <= 0, "");
static_assert(kRank >= 0, "");
static_assert(kPopularity >= 0, "");
static_assert(kErrorsMade <= 0, "");

double TransformDistance(double distance)
{
  return min(distance, RankingInfo::kMaxDistMeters) / RankingInfo::kMaxDistMeters;
}

void PrintParse(ostringstream & oss, array<TokenRange, Model::TYPE_COUNT> const & ranges,
                size_t numTokens)
{
  vector<Model::Type> types(numTokens, Model::Type::TYPE_COUNT);
  for (size_t i = 0; i < ranges.size(); ++i)
  {
    for (size_t pos : ranges[i])
    {
      CHECK_LESS(pos, numTokens, ());
      CHECK_EQUAL(types[pos], Model::Type::TYPE_COUNT, ());
      types[pos] = static_cast<Model::Type>(i);
    }
  }

  oss << "Parse [";
  for (size_t i = 0; i < numTokens; ++i)
  {
    if (i > 0)
      oss << " ";
    oss << DebugPrint(types[i]);
  }
  oss << "]";
}

class BaseTypesChecker
{
protected:
  std::vector<uint32_t> m_types;

public:
  bool operator() (feature::TypesHolder const & th) const
  {
    return base::AnyOf(m_types, [&th](uint32_t t) { return th.HasWithSubclass(t); });
  }
};

class IsAttraction : public BaseTypesChecker
{
public:
  IsAttraction()
  {
    // We have several lists for attractions: short list in search categories for @tourism and long
    // list in ftypes::AttractionsChecker. We have highway-pedestrian, place-square, historic-tomb,
    // landuse-cemetery, amenity-townhall etc in long list and logic of long list is "if this object
    // has high popularity and/or wiki description probably it is attraction". It's better to use
    // short list here.
    m_types = search::GetCategoryTypes("sights", "en", GetDefaultCategories());

    // Add _attraction_ leisures too!
    base::StringIL const types[] = {
      {"leisure", "beach_resort"},
      {"leisure", "garden"},
      {"leisure", "marina"},
      {"leisure", "nature_reserve"},
      {"leisure", "park"},
    };

    Classificator const & c = classif();
    for (auto const & e : types)
      m_types.push_back(c.GetTypeByPath(e));
  }
};

class IsShopOrAmenity : public BaseTypesChecker
{
public:
  IsShopOrAmenity()
  {
    base::StringIL const types[] = {
      {"shop"},

      // Amenity types are very fragmented, so take only most _interesting_ here.
      {"amenity", "bank"},
      {"amenity", "brothel"},
      {"amenity", "car_rental"},
      {"amenity", "casino"},
      {"amenity", "cinema"},
      {"amenity", "clinic"},
      {"amenity", "hospital"},
      {"amenity", "library"},
      {"amenity", "marketplace"},
      {"amenity", "nightclub"},
      {"amenity", "pharmacy"},
      {"amenity", "post_office"},
      {"amenity", "stripclub"},
      {"amenity", "theatre"},
    };

    Classificator const & c = classif();
    for (auto const & e : types)
      m_types.push_back(c.GetTypeByPath(e));
  }
};

class IsServiceTypeChecker : public BaseTypesChecker
{
public:
  IsServiceTypeChecker()
  {
    Classificator const & c = classif();
    for (char const * e : {"barrier", "power", "traffic_calming"})
      m_types.push_back(c.GetTypeByPath({e}));
  }
};
}  // namespace

// static
void RankingInfo::PrintCSVHeader(ostream & os)
{
  os << "DistanceToPivot"
     << ",Rank"
     << ",Popularity"
     << ",Rating"
     << ",NameScore"
     << ",ErrorsMade"
     << ",MatchedFraction"
     << ",SearchType"
     << ",ResultType"
     << ",PureCats"
     << ",FalseCats"
     << ",AllTokensUsed"
     << ",ExactCountryOrCapital"
     << ",IsCategorialRequest"
     << ",HasName";
}

std::string DebugPrint(StoredRankingInfo const & info)
{
  ostringstream os;
  os << "StoredRankingInfo "
     << "{ m_distanceToPivot: " << info.m_distanceToPivot
     << ", m_type: " << DebugPrint(info.m_type)
     << ", m_classifType: ";

  if (Model::IsPoi(info.m_type))
    os << DebugPrint(info.m_classifType.poi);
  else if (info.m_type == Model::TYPE_STREET)
    os << DebugPrint(info.m_classifType.street);

  os << " }";
  return os.str();
}

string DebugPrint(RankingInfo const & info)
{
  ostringstream os;
  os << boolalpha << "RankingInfo { "
     << DebugPrint(static_cast<StoredRankingInfo const &>(info)) << ", ";

  PrintParse(os, info.m_tokenRanges, info.m_numTokens);

  os << ", m_rank: " << static_cast<int>(info.m_rank)
     << ", m_popularity: " << static_cast<int>(info.m_popularity)
     << ", m_nameScore: " << DebugPrint(info.m_nameScore)
     << ", m_errorsMade: " << DebugPrint(info.m_errorsMade)
     << ", m_isAltOrOldName: " << info.m_isAltOrOldName
     << ", m_numTokens: " << info.m_numTokens
     << ", m_commonTokensFactor: " << info.m_commonTokensFactor
     << ", m_matchedFraction: " << info.m_matchedFraction
     << ", m_pureCats: " << info.m_pureCats
     << ", m_falseCats: " << info.m_falseCats
     << ", m_allTokensUsed: " << info.m_allTokensUsed
     << ", m_categorialRequest: " << info.m_categorialRequest
     << ", m_hasName: " << info.m_hasName
     << " }";

  return os.str();
}

void RankingInfo::ToCSV(ostream & os) const
{
  os << fixed;
  os << m_distanceToPivot << ",";
  os << static_cast<int>(m_rank) << ",";
  os << static_cast<int>(m_popularity) << ",";
  os << DebugPrint(m_nameScore) << ",";
  os << GetErrorsMadePerToken() << ",";
  os << m_matchedFraction << ",";
  os << DebugPrint(m_type) << ",";

  if (Model::IsPoi(m_type))
    os << DebugPrint(m_classifType.poi) << ",";
  else if (m_type == Model::TYPE_STREET)
    os << DebugPrint(m_classifType.street) << ",";

  os << m_pureCats << ",";
  os << m_falseCats << ",";
  os << (m_allTokensUsed ? 1 : 0) << ",";
  os << (m_categorialRequest ? 1 : 0) << ",";
  os << (m_hasName ? 1 : 0);
}

double RankingInfo::GetLinearModelRank() const
{
  // NOTE: this code must be consistent with scoring_model.py.  Keep
  // this in mind when you're going to change scoring_model.py or this
  // code. We're working on automatic rank calculation code generator
  // integrated in the build system.
  double const distanceToPivot = TransformDistance(m_distanceToPivot);
  double const rank = static_cast<double>(m_rank) / numeric_limits<uint8_t>::max();
  double const popularity = static_cast<double>(m_popularity) / numeric_limits<uint8_t>::max();

  double result = 0.0;
  if (!m_categorialRequest)
  {
    result += kDistanceToPivot * distanceToPivot;
    result += kRank * rank;
    result += kPopularity * popularity;
    result += kFalseCats * (m_falseCats ? 1 : 0);

    ASSERT(m_type < Model::TYPE_COUNT, ());
    result += kType[GetTypeScore()];

    if (Model::IsPoi(m_type))
    {
      CHECK_LESS(m_classifType.poi, PoiType::Count, ());
      result += kPoiType[base::Underlying(GetPoiTypeScore())];
    }
    else if (m_type == Model::TYPE_STREET)
    {
      CHECK_LESS(m_classifType.street, StreetType::Count, ());
      result += kStreetType[base::Underlying(m_classifType.street)];
    }

    result += (m_allTokensUsed ? 1 : 0) * kAllTokensUsed;
    auto const nameRank = kNameScore[static_cast<size_t>(GetNameScore())] +
                          kErrorsMade * GetErrorsMadePerToken() +
                          kMatchedFraction * m_matchedFraction;
    result += (m_isAltOrOldName ? 0.7 : 1.0) * nameRank;

    result += kCommonTokens * m_commonTokensFactor;
  }
  else
  {
    result += kCategoriesDistanceToPivot * distanceToPivot;
    result += kCategoriesRank * rank;
    result += kCategoriesPopularity * popularity;
    result += kCategoriesFalseCats * (m_falseCats ? 1 : 0);
    result += m_hasName * kHasName;
  }

  return result;
}

// We build LevensteinDFA based on feature tokens to match query.
// Feature tokens can be longer than query tokens that's why every query token can be
// matched to feature token with maximal supported errors number.
// As maximal errors number depends only on tokens number (not tokens length),
// errorsMade per token is supposed to be a good metric.
double RankingInfo::GetErrorsMadePerToken() const
{
  if (!m_errorsMade.IsValid())
    return GetMaxErrorsForTokenLength(numeric_limits<size_t>::max());

  CHECK_GREATER(m_numTokens, 0, ());
  return m_errorsMade.m_errorsMade / static_cast<double>(m_numTokens);
}

NameScore RankingInfo::GetNameScore() const
{
  if (!m_pureCats && m_type == Model::TYPE_SUBPOI && m_nameScore == NameScore::FULL_PREFIX)
  {
    // It's better for ranking when POIs would be equal by name score. Some examples:
    // query="rewe", pois=["REWE", "REWE City", "REWE to Go"]
    // query="carrefour", pois=["Carrefour", "Carrefour Mini", "Carrefour Gurme"]

    // The reason behind that is that user usually does search for _any_ shop within some commercial network.
    // But cities or streets geocoding should distinguish this cases.
    return NameScore::FULL_MATCH;
  }

  return m_nameScore;
}

Model::Type RankingInfo::GetTypeScore() const
{
  return (m_pureCats && m_type == Model::TYPE_BUILDING ? Model::TYPE_UNCLASSIFIED : m_type);
}

PoiType RankingInfo::GetPoiTypeScore() const
{
  // Do not increment score for category-only-matched results or subways will be _always_ on top otherwise.
  return (m_pureCats ? PoiType::General : m_classifType.poi);
}

PoiType GetPoiType(feature::TypesHolder const & th)
{
  using namespace ftypes;

  if (IsEatChecker::Instance()(th))
    return PoiType::Eat;
  if (IsHotelChecker::Instance()(th))
    return PoiType::Hotel;

  if (IsRailwayStationChecker::Instance()(th) ||
      IsSubwayStationChecker::Instance()(th) ||
      IsAirportChecker::Instance()(th))
  {
    return PoiType::TransportMajor;
  }
  if (IsPublicTransportStopChecker::Instance()(th))
    return PoiType::TransportLocal;

  static IsAttraction const attractionCheck;
  if (attractionCheck(th))
    return PoiType::Attraction;

  static IsShopOrAmenity const shopOrAmenityCheck;
  if (shopOrAmenityCheck(th))
    return PoiType::ShopOrAmenity;

  static IsServiceTypeChecker const serviceCheck;
  if (serviceCheck(th))
    return PoiType::Service;

  return PoiType::General;
}

string DebugPrint(PoiType type)
{
  switch (type)
  {
  case PoiType::TransportMajor: return "TransportMajor";
  case PoiType::TransportLocal: return "TransportLocal";
  case PoiType::Eat: return "Eat";
  case PoiType::Hotel: return "Hotel";
  case PoiType::ShopOrAmenity: return "ShopOrAmenity";
  case PoiType::Attraction: return "Attraction";
  case PoiType::Service: return "Service";
  case PoiType::General: return "General";
  case PoiType::Count: return "Count";
  }
  UNREACHABLE();
}

std::string DebugPrint(StreetType type)
{
  switch (type)
  {
  case StreetType::Default: return "Default";
  case StreetType::Pedestrian: return "Pedestrian";
  case StreetType::Cycleway: return "Cycleway";
  case StreetType::Outdoor: return "Outdoor";
  case StreetType::Minors: return "Minors";
  case StreetType::Residential: return "Residential";
  case StreetType::Regular: return "Regular";
  case StreetType::Motorway: return "Motorway";
  case StreetType::Count: return "Count";
  }
  UNREACHABLE();
}

}  // namespace search
