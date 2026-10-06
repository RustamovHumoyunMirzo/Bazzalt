#include "Script/LuaBindings.h"
namespace Bazzalt::Runtime {
template<class T> void Vector(sol::table api,const char* name){
    auto type=api.new_usertype<T>(name,sol::constructors<T(),T(float)>());
    type["X"]=&T::X;type["Y"]=&T::Y;
    type["Length"]=&T::Length;type["LengthSquared"]=&T::LengthSquared;
    type["Normalized"]=[](T value){return value.Normalized();};type["Dot"]=&T::Dot;
    type[sol::meta_function::addition]=[](T a,T b){return a+b;};
    type[sol::meta_function::subtraction]=[](T a,T b){return a-b;};
    type[sol::meta_function::multiplication]=sol::overload([](T a,float b){return a*b;},[](float a,T b){return a*b;});
    type[sol::meta_function::division]=[](T a,float b){return a/b;};
}
template<class T,int N> void Matrix(sol::table api,const char* name){
    auto type=api.new_usertype<T>(name,sol::constructors<T(),T(float)>());
    type["Identity"]=&T::Identity;type["Transposed"]=&T::Transposed;type["Inversed"]=&T::Inversed;
    type["Get"]=[](const T& value,int row,int column){if(row<0||column<0||row>=N||column>=N)throw std::out_of_range("Matrix index");return value(row,column);};
    type["Set"]=[](T& value,int row,int column,float number){if(row<0||column<0||row>=N||column>=N)throw std::out_of_range("Matrix index");value(row,column)=number;};
    type[sol::meta_function::multiplication]=[](const T& a,const T& b){return a*b;};
}
void BindLuaMath(sol::table api){
    api["Pi"]=Pi;api["Epsilon"]=Epsilon;
    api["ToRadians"]=&ToRadians;api["ToDegrees"]=&ToDegrees;api["Clamp"]=&Clamp;api["Lerp"]=&Lerp;
    api["IsNearlyEqual"]=sol::overload([](float a,float b){return IsNearlyEqual(a,b);},[](float a,float b,float epsilon){return IsNearlyEqual(a,b,epsilon);});
    Vector<Vec2>(api,"Vec2");Vector<Vec3>(api,"Vec3");Vector<Vec4>(api,"Vec4");
    sol::usertype<Vec2> v2=api["Vec2"];v2[sol::meta_function::construct]=sol::factories([](){return Vec2{};},[](float v){return Vec2{v};},[](float x,float y){return Vec2{x,y};});v2["Lerp"]=&Vec2::Lerp;
    sol::usertype<Vec3> v3=api["Vec3"];v3["Z"]=&Vec3::Z;v3["Cross"]=&Vec3::Cross;v3["Lerp"]=&Vec3::Lerp;
    v3[sol::meta_function::construct]=sol::factories([](){return Vec3{};},[](float v){return Vec3{v};},[](float x,float y,float z){return Vec3{x,y,z};});
    sol::usertype<Vec4> v4=api["Vec4"];v4["Z"]=&Vec4::Z;v4["W"]=&Vec4::W;v4["XYZ"]=&Vec4::XYZ;
    v4[sol::meta_function::construct]=sol::factories([](){return Vec4{};},[](float v){return Vec4{v};},[](float x,float y,float z,float w){return Vec4{x,y,z,w};});
    Matrix<Mat3,3>(api,"Mat3");Matrix<Mat4,4>(api,"Mat4");
    sol::usertype<Mat3> m3=api["Mat3"];m3["Determinant"]=&Mat3::Determinant;
    m3[sol::meta_function::multiplication]=sol::overload([](Mat3 a,Mat3 b){return a*b;},[](Mat3 a,Vec3 b){return a*b;});
    sol::usertype<Mat4> m4=api["Mat4"];m4["Translation"]=&Mat4::Translation;m4["Scaling"]=&Mat4::Scaling;m4["Rotation"]=&Mat4::Rotation;m4["Transform"]=&Mat4::Transform;
    m4["Perspective"]=&Mat4::Perspective;m4["Orthographic"]=&Mat4::Orthographic;m4["LookAt"]=&Mat4::LookAt;m4["TransformPoint"]=&Mat4::TransformPoint;m4["TransformDirection"]=&Mat4::TransformDirection;
    m4["TryInverse"]=[](const Mat4& value){Mat4 inverse;bool valid=value.TryInverse(inverse);return std::make_tuple(valid,inverse);};
    m4["Decompose"]=[](const Mat4& value){Vec3 p,s;Quaternion r;bool valid=value.Decompose(p,r,s);return std::make_tuple(valid,p,r,s);};
    m4[sol::meta_function::multiplication]=sol::overload([](Mat4 a,Mat4 b){return a*b;},[](Mat4 a,Vec4 b){return a*b;});
    auto q=api.new_usertype<Quaternion>("Quaternion",sol::constructors<Quaternion(),Quaternion(float,float,float,float)>());
    q["X"]=&Quaternion::X;q["Y"]=&Quaternion::Y;q["Z"]=&Quaternion::Z;q["W"]=&Quaternion::W;
    q["Identity"]=&Quaternion::Identity;q["FromAxisAngle"]=&Quaternion::FromAxisAngle;q["FromEuler"]=&Quaternion::FromEuler;
    q["Length"]=&Quaternion::Length;q["LengthSquared"]=&Quaternion::LengthSquared;q["Normalized"]=&Quaternion::Normalized;
    q["Conjugated"]=&Quaternion::Conjugated;q["Inversed"]=&Quaternion::Inversed;q["Rotate"]=&Quaternion::Rotate;q["Dot"]=&Quaternion::Dot;q["Slerp"]=&Quaternion::Slerp;
    q[sol::meta_function::multiplication]=[](Quaternion a,Quaternion b){return a*b;};
    auto uuid=api.new_usertype<UUID>("UUID",sol::constructors<UUID(),UUID(std::uint64_t,std::uint64_t)>());
    uuid["Root"]=&UUID::Root;uuid["Generate"]=&UUID::Generate;uuid["ToString"]=&UUID::ToString;uuid["IsValid"]=&UUID::IsValid;uuid["IsRoot"]=&UUID::IsRoot;
    uuid["GetHigh"]=&UUID::GetHigh;uuid["GetLow"]=&UUID::GetLow;
    uuid["TryParse"]=[](const std::string& text){UUID id;bool valid=UUID::TryParse(text,id);return std::make_tuple(valid,id);};
    uuid["Parse"]=[](const std::string& text){UUID id;if(!UUID::TryParse(text,id))throw std::invalid_argument("Invalid UUID");return id;};
    uuid[sol::meta_function::equal_to]=[](UUID a,UUID b){return a==b;};uuid[sol::meta_function::to_string]=&UUID::ToString;
}
}
