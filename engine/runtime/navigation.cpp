#include <darkangel/navigation.hpp>
#include <recastnavigation/Recast.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
template<class T,void(*Free)(T*)> struct Owned {T* value{};explicit Owned(T* v):value(v){}~Owned(){if(value)Free(value);}Owned(const Owned&)=delete;T* operator->()const{return value;}T& operator*()const{return *value;}};
struct Soup {std::vector<float> vertices;std::vector<int> triangles;
    int vertex(double x,double y,double z){vertices.push_back(float(x));vertices.push_back(float(y));vertices.push_back(float(z));return int(vertices.size()/3)-1;}
};
// Static boxes become twelve triangles in their authored orientation (quaternion, or yaw when none is given).
void add_box(Soup& soup,const CollisionBox& box){
    double x=box.rotation[0],y=box.rotation[1],z=box.rotation[2],w=box.rotation[3];
    if(x==0&&y==0&&z==0&&w==0){y=std::sin(box.yaw*.5);w=std::cos(box.yaw*.5);}
    auto rotate=[&](double px,double py,double pz){
        const double tx=2*(y*pz-z*py),ty=2*(z*px-x*pz),tz=2*(x*py-y*px);
        return MotorVec{px+w*tx+(y*tz-z*ty),py+w*ty+(z*tx-x*tz),pz+w*tz+(x*ty-y*tx)};};
    int corner[8];for(int i=0;i<8;++i){auto p=rotate((i&1?1:-1)*box.half.x,(i&2?1:-1)*box.half.y,(i&4?1:-1)*box.half.z);corner[i]=soup.vertex(box.center.x+p.x,box.center.y+p.y,box.center.z+p.z);}
    static const int faces[12][3]={{2,6,7},{2,7,3},{0,1,5},{0,5,4},{4,5,7},{4,7,6},{0,2,3},{0,3,1},{1,3,7},{1,7,5},{0,4,6},{0,6,2}};
    for(const auto& face:faces)for(int index:face)soup.triangles.push_back(corner[index]);
}
}
struct NavigationMesh::Impl {
    dtNavMesh* mesh{};dtNavMeshQuery* query{};unsigned polygons{};
    ~Impl(){if(query)dtFreeNavMeshQuery(query);if(mesh)dtFreeNavMesh(mesh);}
};
NavigationMesh::NavigationMesh(const CollisionDefinition& collision,const NavigationSettings& settings):impl_(std::make_unique<Impl>()){
    require(settings.cell_size>=.05&&settings.cell_size<=2&&settings.cell_height>=.05&&settings.cell_height<=2&&settings.agent_height>0&&settings.agent_radius>0&&settings.agent_climb>=0&&settings.slope_degrees>0&&settings.slope_degrees<90,"Navigation settings bounds");
    Soup soup;
    for(const auto& box:collision.boxes)if(!box.moving&&!box.dynamic&&!box.sensor)add_box(soup,box);
    for(const auto& mesh:collision.meshes){require(bool(mesh.data),"Navigation needs prepared collision geometry");const int base=int(soup.vertices.size()/3);for(const auto& vertex:mesh.data->vertices)soup.vertex(vertex.x,vertex.y,vertex.z);for(const auto& triangle:mesh.data->triangles)for(auto index:triangle.vertices){require(index<mesh.data->vertices.size(),"Navigation collision triangle index");soup.triangles.push_back(base+int(index));}}
    const int vertex_count=int(soup.vertices.size()/3),triangle_count=int(soup.triangles.size()/3);require(triangle_count>0&&triangle_count<=4000000,"Navigation needs static collision geometry within its triangle bound");
    rcContext context(false);rcConfig config{};
    config.cs=float(settings.cell_size);config.ch=float(settings.cell_height);config.walkableSlopeAngle=float(settings.slope_degrees);
    config.walkableHeight=int(std::ceil(settings.agent_height/settings.cell_height));config.walkableClimb=int(std::floor(settings.agent_climb/settings.cell_height));config.walkableRadius=int(std::ceil(settings.agent_radius/settings.cell_size));
    config.maxEdgeLen=int(12/settings.cell_size);config.maxSimplificationError=1.3f;config.minRegionArea=8*8;config.mergeRegionArea=20*20;config.maxVertsPerPoly=6;config.detailSampleDist=float(settings.cell_size*6);config.detailSampleMaxError=float(settings.cell_height);
    rcCalcBounds(soup.vertices.data(),vertex_count,config.bmin,config.bmax);rcCalcGridSize(config.bmin,config.bmax,config.cs,&config.width,&config.height);
    require(config.width>0&&config.height>0&&config.width<=4096&&config.height<=4096,"Navigation scene is too large for one mesh at this cell size");
    // Either winding of a shallow triangle is walkable ground; steeper ones only block.
    std::vector<unsigned char> areas(triangle_count,0);const double limit=std::cos(settings.slope_degrees*3.141592653589793/180);
    for(int t=0;t<triangle_count;++t){const float* a=&soup.vertices[soup.triangles[t*3]*3];const float* b=&soup.vertices[soup.triangles[t*3+1]*3];const float* c=&soup.vertices[soup.triangles[t*3+2]*3];
        const double ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2],nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,length=std::sqrt(nx*nx+ny*ny+nz*nz);
        if(length>1e-12&&std::abs(ny)/length>=limit)areas[t]=RC_WALKABLE_AREA;}
    Owned<rcHeightfield,rcFreeHeightField> solid(rcAllocHeightfield());require(solid.value&&rcCreateHeightfield(&context,*solid,config.width,config.height,config.bmin,config.bmax,config.cs,config.ch),"Navigation heightfield");
    require(rcRasterizeTriangles(&context,soup.vertices.data(),vertex_count,soup.triangles.data(),areas.data(),triangle_count,*solid,config.walkableClimb),"Navigation rasterization");
    rcFilterLowHangingWalkableObstacles(&context,config.walkableClimb,*solid);rcFilterLedgeSpans(&context,config.walkableHeight,config.walkableClimb,*solid);rcFilterWalkableLowHeightSpans(&context,config.walkableHeight,*solid);
    Owned<rcCompactHeightfield,rcFreeCompactHeightfield> compact(rcAllocCompactHeightfield());require(compact.value&&rcBuildCompactHeightfield(&context,config.walkableHeight,config.walkableClimb,*solid,*compact),"Navigation compact heightfield");
    require(rcErodeWalkableArea(&context,config.walkableRadius,*compact)&&rcBuildDistanceField(&context,*compact)&&rcBuildRegions(&context,*compact,0,config.minRegionArea,config.mergeRegionArea),"Navigation regions");
    Owned<rcContourSet,rcFreeContourSet> contours(rcAllocContourSet());require(contours.value&&rcBuildContours(&context,*compact,config.maxSimplificationError,config.maxEdgeLen,*contours),"Navigation contours");
    Owned<rcPolyMesh,rcFreePolyMesh> polygons(rcAllocPolyMesh());require(polygons.value&&rcBuildPolyMesh(&context,*contours,config.maxVertsPerPoly,*polygons),"Navigation polygon mesh");
    Owned<rcPolyMeshDetail,rcFreePolyMeshDetail> detail(rcAllocPolyMeshDetail());require(detail.value&&rcBuildPolyMeshDetail(&context,*polygons,*compact,config.detailSampleDist,config.detailSampleMaxError,*detail),"Navigation detail mesh");
    require(polygons->npolys>0&&polygons->nverts<0xffff,"Navigation found no walkable ground, or too much for one mesh");
    for(int i=0;i<polygons->npolys;++i)polygons->flags[i]=polygons->areas[i]==RC_WALKABLE_AREA?1:0;
    dtNavMeshCreateParams params{};params.verts=polygons->verts;params.vertCount=polygons->nverts;params.polys=polygons->polys;params.polyAreas=polygons->areas;params.polyFlags=polygons->flags;params.polyCount=polygons->npolys;params.nvp=polygons->nvp;
    params.detailMeshes=detail->meshes;params.detailVerts=detail->verts;params.detailVertsCount=detail->nverts;params.detailTris=detail->tris;params.detailTriCount=detail->ntris;
    params.walkableHeight=float(settings.agent_height);params.walkableRadius=float(settings.agent_radius);params.walkableClimb=float(settings.agent_climb);rcVcopy(params.bmin,polygons->bmin);rcVcopy(params.bmax,polygons->bmax);params.cs=config.cs;params.ch=config.ch;params.buildBvTree=true;
    unsigned char* data{};int size{};require(dtCreateNavMeshData(&params,&data,&size),"Navigation mesh data");
    impl_->mesh=dtAllocNavMesh();if(!impl_->mesh||dtStatusFailed(impl_->mesh->init(data,size,DT_TILE_FREE_DATA))){dtFree(data);throw std::runtime_error("Navigation mesh initialisation");}
    impl_->query=dtAllocNavMeshQuery();require(impl_->query&&dtStatusSucceed(impl_->query->init(impl_->mesh,2048)),"Navigation query initialisation");impl_->polygons=unsigned(polygons->npolys);
}
NavigationMesh::~NavigationMesh()=default;
unsigned NavigationMesh::polygons()const{return impl_->polygons;}
std::optional<std::vector<MotorVec>> NavigationMesh::path(MotorVec from,MotorVec to,unsigned limit)const{
    require(limit>=2&&limit<=256&&std::isfinite(from.x)&&std::isfinite(from.y)&&std::isfinite(from.z)&&std::isfinite(to.x)&&std::isfinite(to.y)&&std::isfinite(to.z),"Navigation path query bounds");
    dtQueryFilter filter;filter.setIncludeFlags(1);const float extents[3]{1,2,1};const float start[3]{float(from.x),float(from.y),float(from.z)},end[3]{float(to.x),float(to.y),float(to.z)};
    dtPolyRef first{},last{};float on_start[3],on_end[3];
    if(dtStatusFailed(impl_->query->findNearestPoly(start,extents,&filter,&first,on_start))||!first||dtStatusFailed(impl_->query->findNearestPoly(end,extents,&filter,&last,on_end))||!last)return std::nullopt;
    dtPolyRef corridor[256];int count{};if(dtStatusFailed(impl_->query->findPath(first,last,on_start,on_end,&filter,corridor,&count,256))||count<=0||corridor[count-1]!=last)return std::nullopt;
    std::vector<float> corners(std::size_t(limit)*3);int points{};if(dtStatusFailed(impl_->query->findStraightPath(on_start,on_end,corridor,count,corners.data(),nullptr,nullptr,&points,int(limit)))||points<1)return std::nullopt;
    std::vector<MotorVec> result;for(int i=0;i<points;++i)result.push_back({corners[i*3],corners[i*3+1],corners[i*3+2]});return result;
}
namespace {
class PathFollower final:public ActorController {
public:
    PathFollower(std::shared_ptr<const NavigationMesh> mesh,PathFollow follow):mesh_(std::move(mesh)),follow_(follow){}
    ControllerCommand decide(const ControllerView& view)override{
        ControllerCommand command;status_="Idle";const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;
        const auto& self=view.self.position;const auto& goal=foe->motor.position;const double dx=goal.x-self.x,dz=goal.z-self.z,distance=std::hypot(dx,dz);
        if(distance<=follow_.arrive){
            // In reach: the scripted attack, no more often than its interval.
            if(distance>1e-6)command.face=std::atan2(dx,dz);
            if(!view.busy&&view.tick>=follow_.attack.first_tick&&(!pressed_||view.tick-pressed_>=follow_.attack.interval)){command.press=follow_.attack.slot;pressed_=view.tick;}
            path_.clear();status_="Attack in reach";return command;}
        if(view.busy)return command;
        if(path_.size()<2||view.tick>=repath_){auto found=mesh_->path(self,goal);path_=found?std::move(*found):std::vector<MotorVec>{};next_=1;repath_=view.tick+follow_.repath_ticks;}
        if(path_.size()<2){status_="No path";return command;}
        status_="Following path";while(next_+1<path_.size()&&std::hypot(path_[next_].x-self.x,path_[next_].z-self.z)<.3)++next_;
        const double cx=path_[next_].x-self.x,cz=path_[next_].z-self.z,length=std::hypot(cx,cz);if(length<1e-6)return command;
        command.move_x=cx/length;command.move_z=cz/length;command.face=std::atan2(cx,cz);return command;
    }
    std::string status()const override{return status_;}
private:std::string status_{"Idle"};std::shared_ptr<const NavigationMesh> mesh_;PathFollow follow_;std::vector<MotorVec> path_;std::size_t next_{1};std::uint64_t repath_{},pressed_{};
};
}
std::shared_ptr<ActorController> make_path_follower(std::shared_ptr<const NavigationMesh> mesh,PathFollow follow){
    require(bool(mesh)&&static_cast<unsigned>(follow.attack.slot)<combat_slot_count&&follow.attack.interval&&std::isfinite(follow.arrive)&&follow.arrive>0&&follow.arrive<=30&&follow.repath_ticks&&follow.repath_ticks<=600,"Path follower bounds");
    return std::make_shared<PathFollower>(std::move(mesh),follow);
}
}
