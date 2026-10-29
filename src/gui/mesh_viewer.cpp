#include "gui/mesh_viewer.h"
#include "platform/os_layer.h"
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gfx {
namespace {
#define MESH_GL(X) \
 X(GenBuffers,PFNGLGENBUFFERSPROC) X(DeleteBuffers,PFNGLDELETEBUFFERSPROC) X(BindBuffer,PFNGLBINDBUFFERPROC) X(BufferData,PFNGLBUFFERDATAPROC) \
 X(GenQueries,PFNGLGENQUERIESPROC) X(DeleteQueries,PFNGLDELETEQUERIESPROC) X(BeginQuery,PFNGLBEGINQUERYPROC) X(EndQuery,PFNGLENDQUERYPROC) X(GetQueryObjectuiv,PFNGLGETQUERYOBJECTUIVPROC) \
 X(GetBufferSubData,PFNGLGETBUFFERSUBDATAPROC) X(BindBufferBase,PFNGLBINDBUFFERBASEPROC) X(GetIntegeri_v,PFNGLGETINTEGERI_VPROC) \
 X(GenVertexArrays,PFNGLGENVERTEXARRAYSPROC) X(DeleteVertexArrays,PFNGLDELETEVERTEXARRAYSPROC) X(BindVertexArray,PFNGLBINDVERTEXARRAYPROC) \
 X(VertexAttribPointer,PFNGLVERTEXATTRIBPOINTERPROC) X(VertexAttribIPointer,PFNGLVERTEXATTRIBIPOINTERPROC) X(EnableVertexAttribArray,PFNGLENABLEVERTEXATTRIBARRAYPROC) \
 X(CreateShader,PFNGLCREATESHADERPROC) X(ShaderSource,PFNGLSHADERSOURCEPROC) X(CompileShader,PFNGLCOMPILESHADERPROC) X(GetShaderiv,PFNGLGETSHADERIVPROC) \
 X(GetShaderInfoLog,PFNGLGETSHADERINFOLOGPROC) X(DeleteShader,PFNGLDELETESHADERPROC) X(CreateProgram,PFNGLCREATEPROGRAMPROC) X(AttachShader,PFNGLATTACHSHADERPROC) \
 X(LinkProgram,PFNGLLINKPROGRAMPROC) X(GetProgramiv,PFNGLGETPROGRAMIVPROC) X(GetProgramInfoLog,PFNGLGETPROGRAMINFOLOGPROC) X(DeleteProgram,PFNGLDELETEPROGRAMPROC) \
 X(UseProgram,PFNGLUSEPROGRAMPROC) X(TransformFeedbackVaryings,PFNGLTRANSFORMFEEDBACKVARYINGSPROC) X(BeginTransformFeedback,PFNGLBEGINTRANSFORMFEEDBACKPROC) X(EndTransformFeedback,PFNGLENDTRANSFORMFEEDBACKPROC) \
 X(GetUniformLocation,PFNGLGETUNIFORMLOCATIONPROC) X(Uniform1i,PFNGLUNIFORM1IPROC) X(UniformMatrix4fv,PFNGLUNIFORMMATRIX4FVPROC) X(GetUniformBlockIndex,PFNGLGETUNIFORMBLOCKINDEXPROC) X(UniformBlockBinding,PFNGLUNIFORMBLOCKBINDINGPROC) X(GetActiveUniformBlockiv,PFNGLGETACTIVEUNIFORMBLOCKIVPROC) \
 X(GenFramebuffers,PFNGLGENFRAMEBUFFERSPROC) X(DeleteFramebuffers,PFNGLDELETEFRAMEBUFFERSPROC) X(BindFramebuffer,PFNGLBINDFRAMEBUFFERPROC) X(FramebufferTexture2D,PFNGLFRAMEBUFFERTEXTURE2DPROC) X(CheckFramebufferStatus,PFNGLCHECKFRAMEBUFFERSTATUSPROC) \
 X(GenRenderbuffers,PFNGLGENRENDERBUFFERSPROC) X(DeleteRenderbuffers,PFNGLDELETERENDERBUFFERSPROC) X(BindRenderbuffer,PFNGLBINDRENDERBUFFERPROC) X(RenderbufferStorage,PFNGLRENDERBUFFERSTORAGEPROC) X(FramebufferRenderbuffer,PFNGLFRAMEBUFFERRENDERBUFFERPROC) X(ActiveTexture,PFNGLACTIVETEXTUREPROC)
struct Gl {
#define DECL(name,type) type name = nullptr;
    MESH_GL(DECL)
#undef DECL
    Gl() {
#define LOAD(name,type) name = reinterpret_cast<type>(os::GlFunction("gl" #name)); if (!name) throw std::runtime_error("OpenGL mesh function missing: gl" #name);
        MESH_GL(LOAD)
#undef LOAD
    }
};
struct State {
    Gl& gl; GLint program, vao, array, fbo, renderbuffer, uniform, feedback, feedbackBase, activeTexture, texture0, viewport[4], polygon[2], depthFunction;
    GLfloat clear[4], pointSize; GLboolean depthMask; bool depth, blend, cull, scissor, discard, srgb; std::vector<GLint> bindings;
    State(Gl& g, size_t uniforms = 0) : gl(g) {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo); glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer); glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&uniform); glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,&feedback); gl.GetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,0,&feedbackBase);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture); gl.ActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0);
        glGetIntegerv(GL_POLYGON_MODE,polygon); glGetIntegerv(GL_DEPTH_FUNC,&depthFunction); glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask); glGetFloatv(GL_COLOR_CLEAR_VALUE,clear); glGetFloatv(GL_POINT_SIZE,&pointSize);
        depth=glIsEnabled(GL_DEPTH_TEST); blend=glIsEnabled(GL_BLEND); cull=glIsEnabled(GL_CULL_FACE); scissor=glIsEnabled(GL_SCISSOR_TEST); discard=glIsEnabled(GL_RASTERIZER_DISCARD); srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
        for (size_t i=0;i<uniforms;++i) { GLint buffer; gl.GetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,static_cast<GLuint>(i),&buffer); bindings.push_back(buffer); }
    }
    ~State() {
        gl.UseProgram(program); gl.BindVertexArray(vao); gl.BindBuffer(GL_ARRAY_BUFFER,array); gl.BindFramebuffer(GL_FRAMEBUFFER,fbo); gl.BindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
        for (size_t i=0;i<bindings.size();++i) gl.BindBufferBase(GL_UNIFORM_BUFFER,static_cast<GLuint>(i),bindings[i]);
        gl.BindBuffer(GL_UNIFORM_BUFFER,uniform); gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedbackBase); gl.BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
        glBindTexture(GL_TEXTURE_2D,texture0); gl.ActiveTexture(activeTexture); glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glPolygonMode(GL_FRONT_AND_BACK,polygon[0]); glDepthFunc(depthFunction); glDepthMask(depthMask); glClearColor(clear[0],clear[1],clear[2],clear[3]); glPointSize(pointSize);
        auto restore=[](GLenum cap,bool enabled){ if(enabled)glEnable(cap);else glDisable(cap); };
        restore(GL_DEPTH_TEST,depth);restore(GL_BLEND,blend);restore(GL_CULL_FACE,cull);restore(GL_SCISSOR_TEST,scissor);restore(GL_RASTERIZER_DISCARD,discard);restore(GL_FRAMEBUFFER_SRGB,srgb);
    }
};
GLuint Shader(Gl& gl, GLenum stage, const std::string& source) {
    const GLuint object=gl.CreateShader(stage); const char* text=source.c_str(); gl.ShaderSource(object,1,&text,nullptr); gl.CompileShader(object);
    GLint okay; gl.GetShaderiv(object,GL_COMPILE_STATUS,&okay);
    if (!okay) { char log[8192]{}; gl.GetShaderInfoLog(object,sizeof(log),nullptr,log); gl.DeleteShader(object); throw std::runtime_error(std::string("Local preview shader: ")+log); }
    return object;
}
GLuint Program(Gl& gl,const std::string& vertex,const std::string& fragment,bool feedback=false) {
    const auto vs=Shader(gl,GL_VERTEX_SHADER,vertex); GLuint fs=0,program=0;
    try {
        if(!fragment.empty())fs=Shader(gl,GL_FRAGMENT_SHADER,fragment); program=gl.CreateProgram(); gl.AttachShader(program,vs);if(fs)gl.AttachShader(program,fs);
        if(feedback){const char* name="gl_Position";gl.TransformFeedbackVaryings(program,1,&name,GL_INTERLEAVED_ATTRIBS);} gl.LinkProgram(program);
        GLint okay;gl.GetProgramiv(program,GL_LINK_STATUS,&okay);if(!okay){char log[8192]{};gl.GetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(std::string("Local preview link: ")+log);}
    } catch(...){if(program)gl.DeleteProgram(program);if(fs)gl.DeleteShader(fs);gl.DeleteShader(vs);throw;}
    gl.DeleteShader(vs);if(fs)gl.DeleteShader(fs);return program;
}
using Matrix=std::array<float,16>;
Matrix Identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix Mul(const Matrix&a,const Matrix&b){Matrix c{};for(int x=0;x<4;++x)for(int y=0;y<4;++y)for(int k=0;k<4;++k)c[x*4+y]+=a[k*4+y]*b[x*4+k];return c;}
}
struct MeshViewer::Impl {
    Gl gl; std::shared_ptr<MeshData> data; std::vector<MeshPosition> screen;
    GLuint program=0,vao=0,vbo=0,fbo=0,color=0,depth=0; int width=0,height=0,space=0,mode=0,attribute=0;
    int uploadedSpace=-1,uploadedAttribute=-1; GLsizei count=0; bool dirty=true; float yaw=.5f,pitch=.25f,distance=3,panX=0,panY=0,screenZoom=1,screenPanX=0,screenPanY=0;
    std::string diagnostic;
    std::array<uint8_t,4> centerPixel{};
    Impl(){
        State state(gl);
        program=Program(gl,R"(#version 330
layout(location=0) in vec4 position;
uniform mat4 transform;
out vec3 viewPosition;
void main(){gl_Position=transform*position;viewPosition=gl_Position.xyz;}
)",R"(#version 330
in vec3 viewPosition;out vec4 color;uniform int mode;
void main(){vec3 n=cross(dFdx(viewPosition),dFdy(viewPosition));float l=length(n)>1e-10?abs(normalize(n).z):1.0;color=vec4(vec3(.28,.66,.88)*(mode==0?(.4+.6*l):1.0),1.0);}
)");
        gl.GenVertexArrays(1,&vao);gl.GenBuffers(1,&vbo);gl.GenFramebuffers(1,&fbo);gl.GenRenderbuffers(1,&depth);glGenTextures(1,&color);
    }
    ~Impl(){gl.DeleteProgram(program);gl.DeleteVertexArrays(1,&vao);gl.DeleteBuffers(1,&vbo);gl.DeleteFramebuffers(1,&fbo);gl.DeleteRenderbuffers(1,&depth);glDeleteTextures(1,&color);}
    void Evaluate(){
        if(data->vertexSource.empty()){diagnostic=data->postDiagnostic;return;}
        State state(gl,data->uniforms.size());GLuint vertexProgram=0,vertexArray=0,feedbackBuffer=0,query=0;std::vector<GLuint> buffers;
        try {
            vertexProgram=Program(gl,data->vertexSource,"",true);gl.GenVertexArrays(1,&vertexArray);gl.BindVertexArray(vertexArray);gl.UseProgram(vertexProgram);
            auto buffer=[&](GLenum target,const void* bytes,size_t length){GLuint b;gl.GenBuffers(1,&b);buffers.push_back(b);gl.BindBuffer(target,b);gl.BufferData(target,static_cast<GLsizeiptr>(length),bytes,GL_STATIC_DRAW);return b;};
            for(const auto& attr:data->attributes) if(std::find(data->shaderInputs.begin(),data->shaderInputs.end(),attr.location)!=data->shaderInputs.end()) {
                if(attr.integer){buffer(GL_ARRAY_BUFFER,attr.integers.data(),attr.integers.size()*sizeof(attr.integers[0]));gl.VertexAttribIPointer(attr.location,4,attr.signedInteger?GL_INT:GL_UNSIGNED_INT,0,nullptr);}
                else {buffer(GL_ARRAY_BUFFER,attr.values.data(),attr.values.size()*sizeof(MeshPosition));gl.VertexAttribPointer(attr.location,4,GL_FLOAT,GL_FALSE,0,nullptr);}gl.EnableVertexAttribArray(attr.location);
            }
            buffer(GL_ARRAY_BUFFER,data->vertexIds.data(),data->vertexIds.size()*4);gl.VertexAttribIPointer(data->idLocation,1,GL_INT,0,nullptr);gl.EnableVertexAttribArray(data->idLocation);
            gl.Uniform1i(gl.GetUniformLocation(vertexProgram,"gfx_original_instance_id"),static_cast<GLint>(data->firstInstance+data->instance));
            GLint requiredBlocks=0,boundBlocks=0;gl.GetProgramiv(vertexProgram,GL_ACTIVE_UNIFORM_BLOCKS,&requiredBlocks);
            for(size_t i=0;i<data->uniforms.size();++i){const auto& u=data->uniforms[i];const auto block=gl.GetUniformBlockIndex(vertexProgram,u.block.c_str());if(block==GL_INVALID_INDEX)continue;
                GLint size=0;gl.GetActiveUniformBlockiv(vertexProgram,block,GL_UNIFORM_BLOCK_DATA_SIZE,&size);if(size>static_cast<GLint>(u.bytes.size()))throw std::runtime_error("Local uniform layout exceeds the captured readback");
                const auto b=buffer(GL_UNIFORM_BUFFER,u.bytes.data(),u.bytes.size());gl.UniformBlockBinding(vertexProgram,block,static_cast<GLuint>(i));gl.BindBufferBase(GL_UNIFORM_BUFFER,static_cast<GLuint>(i),b);++boundBlocks;}
            if(boundBlocks!=requiredBlocks)throw std::runtime_error("An active vertex uniform block could not be mapped to captured data");
            gl.GenBuffers(1,&feedbackBuffer);gl.BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedbackBuffer);screen.resize(data->vertexIds.size());
            gl.BufferData(GL_TRANSFORM_FEEDBACK_BUFFER,static_cast<GLsizeiptr>(screen.size()*sizeof(MeshPosition)),nullptr,GL_STREAM_READ);gl.BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedbackBuffer);
            gl.GenQueries(1,&query);gl.BeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,query);
            glEnable(GL_RASTERIZER_DISCARD);gl.BeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,static_cast<GLsizei>(screen.size()));gl.EndTransformFeedback();
            gl.EndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);GLuint written=0;gl.GetQueryObjectuiv(query,GL_QUERY_RESULT,&written);
            if(written!=screen.size())throw std::runtime_error("Post-VS output count did not match the captured draw");
            gl.GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,static_cast<GLsizeiptr>(screen.size()*sizeof(MeshPosition)),screen.data());
            for(const auto& p:screen)for(float v:p)if(!std::isfinite(v))throw std::runtime_error("Post-VS output contains non-finite positions");
            diagnostic="Post-VS evaluated from the captured vertex module and replay-readback inputs in local OpenGL.";
        }catch(const std::exception& error){screen.clear();diagnostic=error.what();}
        if(query)gl.DeleteQueries(1,&query);if(feedbackBuffer)gl.DeleteBuffers(1,&feedbackBuffer);if(!buffers.empty())gl.DeleteBuffers(static_cast<GLsizei>(buffers.size()),buffers.data());
        if(vertexArray)gl.DeleteVertexArrays(1,&vertexArray);if(vertexProgram)gl.DeleteProgram(vertexProgram);
    }
    void Render(int w,int h){
        State state(gl);gl.BindFramebuffer(GL_FRAMEBUFFER,fbo);gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);
        if(width!=w||height!=h){width=w;height=h;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);gl.BindRenderbuffer(GL_RENDERBUFFER,depth);gl.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);}
        if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Mesh preview framebuffer is incomplete");
        const auto& positions=space?screen:data->attributes.at(attribute).values;
        gl.BindVertexArray(vao);gl.BindBuffer(GL_ARRAY_BUFFER,vbo);
        if(uploadedSpace!=space||uploadedAttribute!=attribute){std::vector<MeshPosition> expanded;expanded.reserve(data->primitiveIndices.size());
            for(auto index:data->primitiveIndices){auto p=positions.at(index);
                if(space){
                    if(data->viewport.is_object()&&data->width&&data->height){const auto& vp=data->viewport;const float sx=vp.value("width",float(data->width))/data->width,sy=vp.value("height",float(data->height))/data->height;
                        p[0]=p[0]*sx+p[3]*(2*vp.value("x",0.0f)/data->width+sx-1);p[1]=-p[1]*sy+p[3]*(1-2*vp.value("y",0.0f)/data->height-sy);}
                    else p[1]=-p[1];p[2]=2*p[2]-p[3];
                }else p[3]=1;expanded.push_back(p);}
            count=static_cast<GLsizei>(expanded.size());gl.BufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(expanded.size()*sizeof(MeshPosition)),expanded.data(),GL_STATIC_DRAW);
            gl.VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,nullptr);gl.EnableVertexAttribArray(0);uploadedSpace=space;uploadedAttribute=attribute;}
        Matrix transform=Identity();
        if(space){transform[0]=transform[5]=screenZoom;transform[12]=screenPanX;transform[13]=screenPanY;}
        if(!space){MeshPosition lo{INFINITY,INFINITY,INFINITY,1},hi{-INFINITY,-INFINITY,-INFINITY,1};for(const auto&p:positions)for(int c=0;c<3;++c){lo[c]=std::min(lo[c],p[c]);hi[c]=std::max(hi[c],p[c]);}
            const float scale=2/std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2],.00001f});Matrix center=Identity();for(int c=0;c<3;++c){center[c*4+c]=scale;center[12+c]=-(hi[c]+lo[c])*.5f*scale;}
            const float radius=.5f*scale*std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]));
            Matrix rx=Identity(),ry=Identity(),view=Identity();rx[5]=std::cos(pitch);rx[6]=std::sin(pitch);rx[9]=-std::sin(pitch);rx[10]=std::cos(pitch);
            ry[0]=std::cos(yaw);ry[2]=-std::sin(yaw);ry[8]=std::sin(yaw);ry[10]=std::cos(yaw);view[12]=panX;view[13]=panY;view[14]=-distance*std::max(radius,.01f)*std::max(1.0f,float(h)/w);
            Matrix projection{};const float f=2.41421356f;projection[0]=f*h/w;projection[5]=f;projection[10]=-1.0002f;projection[11]=-1;projection[14]=-.020002f;
            transform=Mul(projection,Mul(view,Mul(ry,Mul(rx,center))));}
        glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_FRAMEBUFFER_SRGB);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);
        glClearColor(.045f,.055f,.07f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);gl.UseProgram(program);gl.UniformMatrix4fv(gl.GetUniformLocation(program,"transform"),1,GL_FALSE,transform.data());gl.Uniform1i(gl.GetUniformLocation(program,"mode"),mode);
        glPolygonMode(GL_FRONT_AND_BACK,mode==1?GL_LINE:GL_FILL);glPointSize(3);
        glDrawArrays(mode==2||data->primitiveSize==1?GL_POINTS:data->primitiveSize==2?GL_LINES:GL_TRIANGLES,0,count);
#ifdef IMGUI_TEMPLATE_TESTS
        glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,centerPixel.data());
#endif
        dirty=false;
    }
};
MeshViewer::MeshViewer()=default;MeshViewer::~MeshViewer()=default;
void MeshViewer::Release(){impl_.reset();}
void MeshViewer::SetData(std::shared_ptr<MeshData> data){Release();impl_=std::make_unique<Impl>();impl_->data=std::move(data);impl_->space=impl_->data->attributes.empty()?1:0;for(size_t i=0;i<impl_->data->attributes.size();++i)if(impl_->data->attributes[i].location==0){impl_->attribute=static_cast<int>(i);break;}impl_->Evaluate();}
size_t MeshViewer::ScreenVertexCount()const{return impl_?impl_->screen.size():0;}
MeshPosition MeshViewer::ScreenVertex(size_t index)const{return impl_->screen.at(index);}
size_t MeshViewer::InputVertexCount()const{return impl_&&!impl_->data->attributes.empty()?impl_->data->attributes.front().values.size():0;}
const std::string& MeshViewer::Diagnostic()const{static const std::string empty;return impl_?impl_->diagnostic:empty;}
std::array<uint8_t,4> MeshViewer::PreviewCenter()const{return impl_?impl_->centerPixel:std::array<uint8_t,4>{};}
void MeshViewer::Draw(){
    if(!impl_){ImGui::TextDisabled("Select a draw and load its mesh readback.");return;}auto& m=*impl_;
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::max(100.0f,std::min(180.0f,available*.55f)));m.dirty|=ImGui::Combo("##mesh-space",&m.space,"Local (input)\0Screen (post-VS)\0");ImGui::SameLine();ImGui::SetNextItemWidth(std::max(75.0f,std::min(100.0f,ImGui::GetContentRegionAvail().x)));m.dirty|=ImGui::Combo("##mesh-mode",&m.mode,"Solid\0Wireframe\0Points\0");
    if(available>350)ImGui::SameLine();
    if(ImGui::Button("Fit")){m.yaw=.5f;m.pitch=.25f;m.distance=3;m.panX=m.panY=m.screenPanX=m.screenPanY=0;m.screenZoom=1;m.dirty=true;}
    if(!m.space&&!m.data->attributes.empty()){std::string label="Location "+std::to_string(m.data->attributes[m.attribute].location);ImGui::SetNextItemWidth(180);
        if(ImGui::BeginCombo("Position",label.c_str())){for(size_t i=0;i<m.data->attributes.size();++i)if(ImGui::Selectable(("Location "+std::to_string(m.data->attributes[i].location)).c_str(),m.attribute==i)){m.attribute=static_cast<int>(i);m.dirty=true;}ImGui::EndCombo();}}
    if((m.space&&m.screen.empty())||(!m.space&&m.data->attributes.empty())){ImGui::TextWrapped("%s",m.space?m.diagnostic.c_str():m.data->diagnostic.c_str());if(m.space&&!m.data->attributes.empty()&&ImGui::Button("Show input geometry")){m.space=0;m.dirty=true;}return;}
    const auto& positions=m.space?m.screen:m.data->attributes[m.attribute].values;
    for (const auto& point : positions) for (int c=0;c<3;++c) if(!std::isfinite(point[c])) { ImGui::TextDisabled("The selected position attribute contains non-finite values."); return; }
    if(!ImGui::BeginTabBar("mesh-views",ImGuiTabBarFlags_AutoSelectNewTabs))return;
    if(ImGui::BeginTabItem("Preview")){
    const auto size=ImGui::GetContentRegionAvail();const int w=std::clamp(static_cast<int>(size.x),32,2048),h=std::clamp(static_cast<int>(size.y-ImGui::GetTextLineHeightWithSpacing()),32,2048);
    if(m.dirty||m.width!=w||m.height!=h)m.Render(w,h);
    ImGui::Image(static_cast<ImTextureID>(m.color),ImVec2(static_cast<float>(w),static_cast<float>(h)),ImVec2(0,1),ImVec2(1,0));
    if(ImGui::IsItemHovered()){
        auto& io=ImGui::GetIO();if(!m.space&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){m.yaw+=io.MouseDelta.x*.01f;m.pitch=std::clamp(m.pitch+io.MouseDelta.y*.01f,-1.55f,1.55f);m.dirty=true;}
        if(!m.space&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){m.panX+=io.MouseDelta.x*.004f*m.distance;m.panY-=io.MouseDelta.y*.004f*m.distance;m.dirty=true;}
        if(!m.space&&io.MouseWheel){m.distance=std::clamp(m.distance*std::pow(.85f,io.MouseWheel),.15f,100.0f);m.dirty=true;}
        if(m.space&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){m.screenPanX+=2*io.MouseDelta.x/w;m.screenPanY-=2*io.MouseDelta.y/h;m.dirty=true;}
        if(m.space&&io.MouseWheel){m.screenZoom=std::clamp(m.screenZoom*std::pow(1.2f,io.MouseWheel),.05f,32.0f);m.dirty=true;}
        // Post-VS evaluation uses reconstructed GLSL locally; device-specific
        // floating-point behavior can differ from replay on the original GPU.
        ImGui::SetItemTooltip("%s | middle drag: pan | wheel: zoom\nTarget: %s | block %llu | submit %llu",m.space?"Screen-space projection":"Left drag: orbit",m.data->target.c_str(),m.data->block,m.data->submit);}
    ImGui::TextDisabled("%zu vertices | %zu primitives | %s",m.data->vertexIds.size(),m.data->primitiveIndices.size()/m.data->primitiveSize,m.space?"Post-VS":"Input");
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",m.diagnostic.c_str(),m.data->diagnostic.c_str());
    ImGui::EndTabItem();}
    if(ImGui::BeginTabItem("Vertices")){
        if(ImGui::BeginTable("mesh-vertices",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable)){
            ImGui::TableSetupColumn("Row");ImGui::TableSetupColumn("Vertex ID");ImGui::TableSetupColumn("X");ImGui::TableSetupColumn("Y");ImGui::TableSetupColumn("Z");ImGui::TableSetupColumn("W");ImGui::TableHeadersRow();
            ImGuiListClipper clip;clip.Begin(static_cast<int>(positions.size()));while(clip.Step())for(int row=clip.DisplayStart;row<clip.DisplayEnd;++row){
                ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%d",row);ImGui::TableNextColumn();ImGui::Text("%u",m.data->vertexIds[row]);
                for(int c=0;c<4;++c){ImGui::TableNextColumn();ImGui::Text("%.7g",positions[row][c]);}}
            ImGui::EndTable();}
        ImGui::EndTabItem();}
    ImGui::EndTabBar();
}
} // namespace gfx
