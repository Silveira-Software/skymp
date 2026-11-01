#include "SrpCollision.h"
#include <sodium.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace SrpCollision {
namespace {
constexpr double epsilon = 1e-9;
constexpr double coordinateLimit = 1e8;
constexpr size_t triangleLimit = 2000000;
void Require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}
bool Finite(Vec3 a)
{
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
    std::abs(a.x) <= coordinateLimit && std::abs(a.y) <= coordinateLimit &&
    std::abs(a.z) <= coordinateLimit;
}
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Cross(Vec3 a, Vec3 b)
{
  return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double Axis(Vec3 a, size_t i) { return i==0?a.x:(i==1?a.y:a.z); }
bool Valid(const Aabb& b)
{
  return Finite(b.min) && Finite(b.max) && b.min.x <= b.max.x &&
    b.min.y <= b.max.y && b.min.z <= b.max.z;
}
bool Valid(const Triangle& t)
{
  return Finite(t.a) && Finite(t.b) && Finite(t.c) &&
    Dot(Cross(Sub(t.b,t.a),Sub(t.c,t.a)),Cross(Sub(t.b,t.a),Sub(t.c,t.a))) > epsilon*epsilon;
}
Vec3 ReadVec(const nlohmann::json& value)
{
  Require(value.is_array() && value.size()==3,"SRP_COLLISION_VECTOR_INVALID");
  Vec3 out{value.at(0).get<double>(),value.at(1).get<double>(),value.at(2).get<double>()};
  Require(Finite(out),"SRP_COLLISION_VECTOR_INVALID");return out;
}
Aabb ReadBox(const nlohmann::json& value)
{
  Aabb out{ReadVec(value.at("min")),ReadVec(value.at("max"))};
  Require(Valid(out),"SRP_COLLISION_BOUNDS_INVALID");return out;
}
bool Hash(const std::string& s)
{
  return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='f');
  });
}
bool Interval(const Aabb& box, Vec3 a, Vec3 b, double& lo, double& hi)
{
  lo=0;hi=1;
  const auto direction=Sub(b,a);
  for (size_t axis=0;axis<3;++axis) {
    const double origin=Axis(a,axis),delta=Axis(direction,axis);
    const double low=Axis(box.min,axis),high=Axis(box.max,axis);
    if (delta==0) { if(origin<low || origin>high)return false;continue; }
    double enter=(low-origin)/delta,leave=(high-origin)/delta;
    if(enter>leave)std::swap(enter,leave);
    lo=std::max(lo,enter);hi=std::min(hi,leave);
    if(lo>hi)return false;
  }
  return true;
}
// Coplanar and edge hits also block. Ignoring a parallel ray would allow a
// segment lying along a wall to incorrectly receive a Clear verdict.
struct Vec2 { double x,y; };
double Orient(Vec2 a,Vec2 b,Vec2 c)
{
  return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
bool Contains(Vec2 p,Vec2 a,Vec2 b,Vec2 c)
{
  const auto x=Orient(a,b,p),y=Orient(b,c,p),z=Orient(c,a,p);
  return !((x < -epsilon || y < -epsilon || z < -epsilon) &&
           (x > epsilon || y > epsilon || z > epsilon));
}
bool Segment2(Vec2 a,Vec2 b,Vec2 c,Vec2 d)
{
  const auto x=Orient(a,b,c),y=Orient(a,b,d),z=Orient(c,d,a),w=Orient(c,d,b);
  if (((x>epsilon && y< -epsilon)||(x< -epsilon && y>epsilon)) &&
      ((z>epsilon && w< -epsilon)||(z< -epsilon && w>epsilon)))return true;
  const auto on=[](Vec2 p,Vec2 q,Vec2 r,double orientation){
    return std::abs(orientation)<=epsilon && r.x>=std::min(p.x,q.x)-epsilon &&
      r.x<=std::max(p.x,q.x)+epsilon && r.y>=std::min(p.y,q.y)-epsilon &&
      r.y<=std::max(p.y,q.y)+epsilon;
  };
  return on(a,b,c,x)||on(a,b,d,y)||on(c,d,a,z)||on(c,d,b,w);
}
bool Hit(const Triangle& t, Vec3 from, Vec3 to, double& fraction)
{
  const auto edge1=Sub(t.b,t.a),edge2=Sub(t.c,t.a),normal=Cross(edge1,edge2);
  const auto direction=Sub(to,from);
  const auto denominator=Dot(normal,direction);
  const auto normalLength=std::sqrt(Dot(normal,normal));
  const double planeDistance=Dot(normal,Sub(from,t.a));
  // Use exact parallelism here; the barycentric path remains well-defined for
  // a near-parallel segment and must not miss a crossing at grazing angles.
  if(denominator==0) {
    if(std::abs(planeDistance)>epsilon*normalLength)return false;
    size_t drop=0;
    if(std::abs(normal.y)>std::abs(normal.x))drop=1;
    if(std::abs(normal.z)>std::abs(Axis(normal,drop)))drop=2;
    const auto project=[&](Vec3 p)->Vec2{
      return drop==0?Vec2{p.y,p.z}:(drop==1?Vec2{p.x,p.z}:Vec2{p.x,p.y});
    };
    const auto a=project(t.a),b=project(t.b),c=project(t.c),p=project(from),q=project(to);
    if(Contains(p,a,b,c)||Contains(q,a,b,c)||Segment2(p,q,a,b)||Segment2(p,q,b,c)||Segment2(p,q,c,a)) {
      fraction=0;return true;
    }
    return false;
  }
  const double f=-planeDistance/denominator;
  if(f< -epsilon || f>1+epsilon)return false;
  const Vec3 point{from.x+direction.x*f,from.y+direction.y*f,from.z+direction.z*f};
  const auto relative=Sub(point,t.a);
  const double d00=Dot(edge1,edge1),d01=Dot(edge1,edge2),d11=Dot(edge2,edge2);
  const double d20=Dot(relative,edge1),d21=Dot(relative,edge2);
  const double det=d00*d11-d01*d01;
  // Extremely ill-conditioned but nondegenerate geometry cannot prove clear.
  if(det<=0) { fraction=std::clamp(f,0.0,1.0);return true; }
  const double u=(d11*d20-d01*d21)/det,v=(d00*d21-d01*d20)/det;
  if(u< -epsilon || v< -epsilon || u+v>1+epsilon)return false;
  fraction=std::clamp(f,0.0,1.0);return true;
}
void Include(Aabb& box,Vec3 p)
{
  box.min={std::min(box.min.x,p.x),std::min(box.min.y,p.y),std::min(box.min.z,p.z)};
  box.max={std::max(box.max.x,p.x),std::max(box.max.y,p.y),std::max(box.max.z,p.z)};
}
uint32_t Location(const nlohmann::json& value)
{
  Require(value.is_number_unsigned() || value.is_number_integer(),"SRP_COLLISION_LOCATION_INVALID");
  const auto id=value.get<int64_t>();
  Require(id>0 && id<=std::numeric_limits<uint32_t>::max(),"SRP_COLLISION_LOCATION_INVALID");
  return static_cast<uint32_t>(id);
}
bool ValidTransform(const std::array<double,12>& matrix)
{
  for(auto value:matrix)if(!std::isfinite(value) || std::abs(value)>coordinateLimit)return false;
  const double determinant=matrix[0]*(matrix[5]*matrix[10]-matrix[6]*matrix[9])-
    matrix[1]*(matrix[4]*matrix[10]-matrix[6]*matrix[8])+matrix[2]*(matrix[4]*matrix[9]-matrix[5]*matrix[8]);
  return std::isfinite(determinant) && std::abs(determinant)>epsilon;
}
std::array<double,12> ReadTransform(const nlohmann::json& transform)
{
  Require(transform.is_array() && transform.size()==12,"SRP_COLLISION_TRANSFORM_INVALID");
  std::array<double,12> matrix;
  for(size_t i=0;i<matrix.size();++i)matrix[i]=transform.at(i).get<double>();
  Require(ValidTransform(matrix),"SRP_COLLISION_TRANSFORM_INVALID");return matrix;
}
Vec3 Transform(const std::array<double,12>& matrix,Vec3 v)
{
  return {matrix[0]*v.x+matrix[1]*v.y+matrix[2]*v.z+matrix[3],
    matrix[4]*v.x+matrix[5]*v.y+matrix[6]*v.z+matrix[7],
    matrix[8]*v.x+matrix[9]*v.y+matrix[10]*v.z+matrix[11]};
}
std::vector<Triangle> ReadMesh(const nlohmann::json& geometry,
  const std::array<double,12>& matrix,size_t& count)
{
  Require(geometry.at("vertices").is_array() && geometry.at("triangles").is_array(),
    "SRP_COLLISION_GEOMETRY_INVALID");
  std::vector<Vec3> vertices;
  Require(geometry.at("vertices").size()<=triangleLimit*3,"SRP_COLLISION_SIZE_LIMIT");
  for(const auto& value:geometry.at("vertices")) {
    const auto p=Transform(matrix,ReadVec(value));
    Require(Finite(p),"SRP_COLLISION_TRANSFORM_INVALID");vertices.push_back(p);
  }
  std::vector<Triangle> triangles;
  for(const auto& indices:geometry.at("triangles")) {
    Require(++count<=triangleLimit,"SRP_COLLISION_SIZE_LIMIT");
    Require(indices.is_array() && indices.size()==3,"SRP_COLLISION_TRIANGLE_INVALID");
    std::array<size_t,3> index;
    for(size_t i=0;i<3;++i) {
      Require(indices.at(i).is_number_integer(),"SRP_COLLISION_TRIANGLE_INVALID");
      const auto value=indices.at(i).get<int64_t>();
      Require(value>=0 && static_cast<uint64_t>(value)<vertices.size(),"SRP_COLLISION_TRIANGLE_INVALID");
      index[i]=static_cast<size_t>(value);
    }
    Triangle triangle{vertices[index[0]],vertices[index[1]],vertices[index[2]]};
    Require(Valid(triangle),"SRP_COLLISION_TRIANGLE_INVALID");triangles.push_back(triangle);
  }
  Require(!triangles.empty(),"SRP_COLLISION_GEOMETRY_EMPTY");return triangles;
}
}

std::string Scene::Digest(const nlohmann::json& document)
{
  const auto bytes=document.dump();unsigned char digest[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(digest,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());
  constexpr char hex[]="0123456789abcdef";std::string result;
  for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
std::shared_ptr<const Scene> Scene::Load(const nlohmann::json& document,
  const nlohmann::json& expectedBindings,const std::string& expectedDocumentSha256)
{
  Require(document.is_object() && document.at("schemaVersion")==1 &&
    document.at("units")=="skyrim-world","SRP_COLLISION_SCHEMA_INVALID");
  Require(Hash(expectedDocumentSha256) && Digest(document)==expectedDocumentSha256,
    "SRP_COLLISION_CONTENT_MISMATCH");
  const auto& binding=document.at("bindings");
  Require(binding==expectedBindings,"SRP_COLLISION_PROVENANCE_MISMATCH");
  Require(binding.is_object() && binding.at("loadOrder").is_array() &&
    !binding.at("loadOrder").empty() && binding.at("assets").is_array() &&
    Hash(binding.at("producerSha256").get<std::string>()) &&
    Hash(binding.at("coverageAuditSha256").get<std::string>()),"SRP_COLLISION_BINDINGS_INVALID");
  std::set<std::string> pluginNames,assetNames;
  for(const auto& plugin:binding.at("loadOrder")) {
    const auto name=plugin.at("name").get<std::string>();
    Require(!name.empty() && pluginNames.insert(name).second &&
      Hash(plugin.at("sha256").get<std::string>()),"SRP_COLLISION_PLUGIN_INVALID");
  }
  for(const auto& asset:binding.at("assets")) {
    const auto name=asset.at("path").get<std::string>();
    Require(!name.empty() && assetNames.insert(name).second &&
      Hash(asset.at("sha256").get<std::string>()),"SRP_COLLISION_ASSET_INVALID");
  }
  auto out=std::shared_ptr<Scene>(new Scene);out->id=expectedDocumentSha256;out->bindings=binding;
  Require(document.at("geometry").is_array() && document.at("coverage").is_array() &&
    document.at("unresolved").is_array(),"SRP_COLLISION_SCHEMA_INVALID");
  size_t count=0;
  std::set<std::tuple<uint32_t,std::string,std::string>> parts;
  for(const auto& geometry:document.at("geometry")) {
    const auto location=Location(geometry.at("worldOrCell"));
    const auto reference=geometry.at("reference").get<std::string>();
    const auto base=geometry.at("base").get<std::string>();
    const auto asset=geometry.at("asset").get<std::string>();
    Require(!reference.empty() && !base.empty() &&
      parts.emplace(location,reference,geometry.value("part",std::string())).second &&
      assetNames.count(geometry.at("asset").get<std::string>()) &&
      geometry.at("representation")=="havok-triangles" &&
      geometry.at("vertices").is_array() && geometry.at("triangles").is_array(),
      "SRP_COLLISION_GEOMETRY_INVALID");
    const auto matrix=ReadTransform(geometry.at("worldTransform"));
    const auto referenceMatrix=ReadTransform(geometry.at("referenceTransform"));
    const auto existing=std::find_if(out->references.begin(),out->references.end(),[&](const ReferenceBinding& item){
      return item.worldOrCell==location && item.reference==reference;
    });
    if(existing==out->references.end())out->references.push_back({reference,base,asset,location,referenceMatrix});
    else Require(existing->base==base && existing->asset==asset && existing->referenceTransform==referenceMatrix,
      "SRP_COLLISION_REFERENCE_MISMATCH");
    Batch batch;batch.worldOrCell=location;
    batch.bounds={{coordinateLimit,coordinateLimit,coordinateLimit},{-coordinateLimit,-coordinateLimit,-coordinateLimit}};
    batch.triangles=ReadMesh(geometry,matrix,count);
    for(const auto& triangle:batch.triangles) {
      Include(batch.bounds,triangle.a);Include(batch.bounds,triangle.b);Include(batch.bounds,triangle.c);
    }
    out->batches.push_back(std::move(batch));
  }
  for(const auto& coverage:document.at("coverage")) {
    Require(coverage.at("complete").is_boolean(),"SRP_COLLISION_COVERAGE_INVALID");
    out->regions.push_back({Location(coverage.at("worldOrCell")),ReadBox(coverage),coverage.at("complete").get<bool>()});
  }
  for(const auto& unknown:document.at("unresolved"))
    out->unresolved.emplace_back(Location(unknown.at("worldOrCell")),ReadBox(unknown));
  if(document.contains("actorShapes")) {
    Require(document.at("actorShapes").is_array(),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
    std::set<std::pair<std::string,std::string>> actorKeys;
    for(const auto& shape:document.at("actorShapes")) {
      const auto base=shape.at("baseDesc").get<std::string>(),race=shape.at("raceDesc").get<std::string>();
      Require(!base.empty() && !race.empty() && actorKeys.emplace(base,race).second &&
        shape.at("policy")=="SERVER_MESH_PROXY_V1" && shape.at("representation")=="havok-triangles" &&
        assetNames.count(shape.at("asset").get<std::string>()),"SRP_COLLISION_ACTOR_SHAPES_INVALID");
      out->actorShapes.push_back({base,race,ReadMesh(shape,ReadTransform(shape.at("localTransform")),count)});
    }
  }
  return out;
}

Result IntersectMesh(Vec3 from, Vec3 to, const std::vector<Triangle>& triangles)
{
  Result result;result.reason="SRP_COLLISION_TARGET_INVALID";
  if(!Finite(from) || !Finite(to) || Dot(Sub(to,from),Sub(to,from))<=epsilon*epsilon ||
    triangles.empty() || triangles.size()>triangleLimit)return result;
  for(const auto& triangle:triangles)if(!Valid(triangle))return result;
  result.verdict=Verdict::Clear;result.reason="SRP_COLLISION_TARGET_MISSED";
  for(const auto& triangle:triangles) {
    double fraction;
    if(Hit(triangle,from,to,fraction)) {
      result.verdict=Verdict::Blocked;result.reason="SRP_COLLISION_TARGET_HIT";
      result.hitFraction=std::min(result.hitFraction,fraction);
    }
  }
  return result;
}
std::optional<std::vector<Triangle>> Scene::ActorTriangles(const std::string& baseDesc,
  const std::string& raceDesc,const std::array<double,12>& serverPose) const
{
  if(!ValidTransform(serverPose))return {};
  const auto found=std::find_if(actorShapes.begin(),actorShapes.end(),[&](const ActorShape& shape){
    return shape.baseDesc==baseDesc && shape.raceDesc==raceDesc;
  });
  if(found==actorShapes.end())return {};
  std::vector<Triangle> triangles;
  for(const auto& source:found->triangles) {
    Triangle t{Transform(serverPose,source.a),Transform(serverPose,source.b),Transform(serverPose,source.c)};
    if(!Valid(t))return {};triangles.push_back(t);
  }
  return triangles;
}

Result Scene::Trace(const Query& query) const
{
  Result result;result.sceneId=id;result.dynamicRevision=query.dynamics.revision;
  result.reason="SRP_COLLISION_QUERY_INVALID";
  if(!query.worldOrCell || !Finite(query.from) || !Finite(query.to) ||
    Dot(Sub(query.to,query.from),Sub(query.to,query.from))<=epsilon*epsilon)return result;
  const auto hit=[&](const Triangle& triangle){
    double fraction;
    if(Hit(triangle,query.from,query.to,fraction)) {
      result.verdict=Verdict::Blocked;result.hitFraction=std::min(result.hitFraction,fraction);
    }
  };
  for(const auto& batch:batches) {
    double lo,hi;
    if(batch.worldOrCell==query.worldOrCell && Interval(batch.bounds,query.from,query.to,lo,hi))
      for(const auto& triangle:batch.triangles)hit(triangle);
  }
  if(result.verdict==Verdict::Blocked) { result.reason="SRP_COLLISION_STATIC_BLOCKED";return result; }
  if(query.dynamics.worldOrCell!=query.worldOrCell || !query.dynamics.revision) {
    result.reason="SRP_COLLISION_DYNAMIC_SNAPSHOT_MISSING";return result;
  }
  if(query.dynamics.triangles.size()>triangleLimit) {
    result.reason="SRP_COLLISION_DYNAMIC_SIZE_LIMIT";return result;
  }
  for(const auto& triangle:query.dynamics.triangles) {
    if(!Valid(triangle)) { result.verdict=Verdict::Unknown;result.reason="SRP_COLLISION_DYNAMIC_GEOMETRY_INVALID";return result; }
    hit(triangle);
  }
  if(result.verdict==Verdict::Blocked) { result.reason="SRP_COLLISION_DYNAMIC_BLOCKED";return result; }
  if(!query.dynamics.complete) { result.reason="SRP_COLLISION_DYNAMIC_COVERAGE_INCOMPLETE";return result; }
  for(const auto& box:query.dynamics.unresolvedBlockers) {
    double lo,hi;
    if(!Valid(box) || Interval(box,query.from,query.to,lo,hi)) {
      result.reason="SRP_COLLISION_DYNAMIC_UNRESOLVED";return result;
    }
  }
  for(const auto& unknown:unresolved) {
    double lo,hi;
    if(unknown.first==query.worldOrCell && Interval(unknown.second,query.from,query.to,lo,hi)) {
      result.reason="SRP_COLLISION_STATIC_UNRESOLVED";return result;
    }
  }
  // A segment must be covered in its entirety. Endpoints in two disconnected
  // audited cells do not prove that the space between them was imported.
  std::vector<std::pair<double,double>> intervals;
  for(const auto& region:regions) {
    double lo,hi;
    if(region.worldOrCell!=query.worldOrCell || !Interval(region.bounds,query.from,query.to,lo,hi))continue;
    if(!region.complete) { result.reason="SRP_COLLISION_STATIC_COVERAGE_INCOMPLETE";return result; }
    intervals.emplace_back(lo,hi);
  }
  std::sort(intervals.begin(),intervals.end());
  double covered=0;bool begins=false;
  for(const auto& interval:intervals) {
    if(interval.first>covered)break;
    begins=true;covered=std::max(covered,interval.second);
    if(covered>=1) { result.verdict=Verdict::Clear;result.reason="SRP_COLLISION_CLEAR";return result; }
  }
  (void)begins;result.reason="SRP_COLLISION_STATIC_COVERAGE_MISSING";return result;
}
}
