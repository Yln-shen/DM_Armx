// test_dm_gravity.cpp —— 重力项：与**已跑通的 MuJoCo** 逐点对拍（守住 sign 那一段符号链），
// 外加 sign 生效检查与参数/用法错误。
//
// 为什么是 MuJoCo：它是另一条完全独立的实现路径，而且本工程 2026-10-05 已经用它算过
// 真机姿态下的重力项并与 pinocchio 逐位一致。这里把这个对拍固化成机器保证。
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "motor_driver_hardware/dm_gravity.hpp"

#ifndef ARM_DYN_URDF
#error "缺少 ARM_DYN_URDF（应由 CMake target_compile_definitions 传入）"
#endif
#ifndef ARM_DESC_DIR
#error "缺少 ARM_DESC_DIR（应由 CMake target_compile_definitions 传入）"
#endif

namespace md = motor_driver_hardware;

namespace
{

const std::vector<std::string> kJoints = {
  "joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};

// MuJoCo 参考：把 package:// 换成真实 mesh 路径（MuJoCo 要开 mesh 文件，pinocchio 不用），
// 然后逐行读 q、打印 qfrc_bias[:6]（零速度 ⇒ 就是纯重力项）。
const char * kPyMujoco = R"PY(
import sys
import numpy as np, mujoco
urdf_path, mesh_root = sys.argv[1], sys.argv[2]
txt = open(urdf_path, encoding="utf-8").read()
txt = txt.replace("package://arm_description/", mesh_root.rstrip("/") + "/")
mj = "/tmp/dm_gravity_mj.urdf"
open(mj, "w", encoding="utf-8").write(txt)
m = mujoco.MjModel.from_xml_path(mj)
for line in sys.stdin:
    q = [float(x) for x in line.split()]
    if not q: continue
    d = mujoco.MjData(m)
    d.qpos[:] = q
    mujoco.mj_forward(m, d)
    print(" ".join("%.17g" % v for v in d.qfrc_bias[:6]))
)PY";

std::vector<std::vector<double>> mujoco_reference(const std::vector<std::vector<double>> & poses)
{
  const std::string script = "/tmp/dm_gravity_ref.py";
  const std::string input = "/tmp/dm_gravity_ref_in.txt";
  {
    std::ofstream f(script);
    f << kPyMujoco;
  }
  {
    std::ofstream f(input);
    char buf[64];
    for (const auto & p : poses) {
      for (std::size_t i = 0; i < p.size(); ++i) {
        // ⚠️ 必须 %.17g：ostream 默认只给 6 位有效数字，1.392202 会被写成 1.3922，
        //    于是两边算的根本不是同一个位姿（曾因此在"正好压限位"的用例上差 1e-5）。
        std::snprintf(buf, sizeof(buf), "%s%.17g", i ? " " : "", p[i]);
        f << buf;
      }
      f << "\n";
    }
  }
  const std::string cmd = std::string("python3 ") + script + " " + ARM_DYN_URDF + " " +
    ARM_DESC_DIR + " < " + input;
  FILE * pipe = popen(cmd.c_str(), "r");
  std::vector<std::vector<double>> out;
  if (pipe == nullptr) {return out;}
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    std::istringstream is(buf);
    std::vector<double> row;
    double v = 0.0;
    while (is >> v) {row.push_back(v);}
    if (row.size() == 6) {out.push_back(row);}
  }
  pclose(pipe);
  return out;
}

std::string urdf_hint()
{
  return std::string("找不到 ") + ARM_DYN_URDF +
         " —— 先 `colcon build --packages-select arm_description`（它会在构建时生成这份纯 URDF）";
}

}  // namespace

// ① 与 MuJoCo 逐点对拍（sign 全取 +1 ⇒ 输出就是 URDF 坐标下的重力项，可直接比）
TEST(DmGravity, MatchesMuJoCo)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << urdf_hint();
  probe.close();

  const std::vector<std::vector<double>> poses = {
    {1.4724, 0.00076, -0.0233, 0.00267, 0.00687, 0.01488},              // 真机实测：折叠位
    {1.4709, -0.5528, -0.7828, 0.6176, -0.0088, 0.0111},                // 真机实测：张开位
    {1.392202, -2.379924, -1.891796, -0.157540, -0.970641, -1.359002},  // 全下界
    {1.626143, 0.058351, 0.050752, 0.938132, 0.913381, 4.924184},       // 全上界
  };
  const auto ref = mujoco_reference(poses);
  ASSERT_EQ(ref.size(), poses.size()) << "MuJoCo 参考没有逐行给结果（脚本/import 出错？）";

  const std::vector<int> sign_plus(6, 1);
  md::GravityModel gm(ARM_DYN_URDF, kJoints, sign_plus);
  ASSERT_EQ(gm.size(), 6u);

  for (std::size_t k = 0; k < poses.size(); ++k) {
    std::vector<double> tau(6, 0.0);
    gm.tau_ours(poses[k].data(), tau.data());
    for (int i = 0; i < 6; ++i) {
      EXPECT_NEAR(tau[i], ref[k][i], 1e-9) << "位姿 #" << k << " 关节 " << kJoints[i];
    }
  }
}

// ② sign 真的生效（逐关节），而且只乘 sign、不加 zero_shift
TEST(DmGravity, SignIsAppliedPerJoint)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << urdf_hint();
  probe.close();

  const std::vector<double> q = {1.4709, -0.5528, -0.7828, 0.6176, -0.0088, 0.0111};
  const std::vector<int> sign_plus(6, 1);
  const std::vector<int> sign_real = {-1, -1, -1, -1, -1, 1};   // align.yaml 的真实值
  md::GravityModel g1(ARM_DYN_URDF, kJoints, sign_plus);
  md::GravityModel g2(ARM_DYN_URDF, kJoints, sign_real);

  std::vector<double> a(6, 0.0), b(6, 0.0);
  g1.tau_ours(q.data(), a.data());
  g2.tau_ours(q.data(), b.data());
  for (int i = 0; i < 6; ++i) {
    EXPECT_DOUBLE_EQ(b[i], static_cast<double>(sign_real[i]) * a[i]) << kJoints[i];
  }
  EXPECT_DOUBLE_EQ(b[5], a[5]) << "j6 的 sign 是 +1，两份输出该完全一样";
  EXPECT_DOUBLE_EQ(b[1], -a[1]) << "j2 的 sign 是 −1，该正好反号";
}

// ③ 参数与用法错误：当场拒，不静默
TEST(DmGravity, RejectsBadInput)
{
  std::ifstream probe(ARM_DYN_URDF);
  ASSERT_TRUE(probe.good()) << urdf_hint();
  probe.close();

  const std::vector<int> sign_plus(6, 1);
  // 模型里没有这个关节名
  EXPECT_THROW(
    md::GravityModel(ARM_DYN_URDF, {"joint1", "根本没有这个关节"}, {1, 1}),
    std::invalid_argument);
  // 长度不一致
  EXPECT_THROW(md::GravityModel(ARM_DYN_URDF, kJoints, {1, 1}), std::invalid_argument);
  // sign 不是 ±1
  EXPECT_THROW(
    md::GravityModel(ARM_DYN_URDF, kJoints, {2, 1, 1, 1, 1, 1}), std::invalid_argument);
  // URDF 根本不存在 ⇒ pinocchio 会抛（具体类型不重要，只要不静默）
  EXPECT_THROW(
    md::GravityModel("/tmp/dm_gravity_没有这个文件.urdf", kJoints, sign_plus), std::exception);

  md::GravityModel gm(ARM_DYN_URDF, kJoints, sign_plus);
  std::vector<double> q(6, 0.0), tau(6, 0.0);
  q[3] = std::nan("");
  EXPECT_THROW(gm.tau_ours(q.data(), tau.data()), std::invalid_argument);
  q[3] = 0.0;
  EXPECT_NO_THROW(gm.tau_ours(q.data(), tau.data()));
}
