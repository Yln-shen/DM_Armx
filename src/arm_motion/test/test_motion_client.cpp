// test_motion_client.cpp —— MotionClient：进程内假 action server + 假 /joint_states。
//
// 覆盖（都不接硬件、不落盘、不起 ros2_control）：
//   ① 越界目标 ⇒ 当场拒绝、一帧不发、无事件（陷阱 #59 的护栏）
//   ② 正常到达 ⇒ ok() 且残差 ≈ 0
//   ③ JTC 报成功但位置没动 ⇒ 残差超差判失败
//   ④ 执行超时 ⇒ **真的取消**并等到确认（陷阱 #60：不取消臂会自己走完）
//   ⑤ 没有 action server ⇒ 超时报"等不到"
//   ⑥ 没有 /joint_states ⇒ 超时报"拒绝发轨迹"
//
// ⚠️ 目标必须是**模型坐标**且落在 `arm_limits::kModelLimits` 内 —— j1 的合法区间是
//    [1.392202, 1.626143]，所以"全 0 目标"是**越界**的（这不是 bug，是护栏在干活）。
//    基准姿态用折叠位（真机实测值，六个关节都在限位内）。
#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "arm_motion/motion_client.hpp"

using namespace std::chrono_literals;
using FollowJointTrajectory = control_msgs::action::FollowJointTrajectory;
using ServerGoalHandle = rclcpp_action::ServerGoalHandle<FollowJointTrajectory>;

namespace
{

const std::vector<std::string> kJoints = {
  "joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};

// 基准姿态 = 折叠位（模型坐标；六个关节都在限位内）
const std::vector<double> kStart = {
  1.4724, 0.0008, -0.0233, 0.0027, 0.0069, 0.0149};

// 造一个合法目标：把第 idx 个关节挪 delta（调用方保证不越限）
std::vector<double> make_target(std::size_t idx, double delta)
{
  std::vector<double> t = kStart;
  t[idx] += delta;
  return t;
}

// 假 JTC：Succeed = 收到就算成功；NeverFinish = 接受但永不结束（逼客户端超时后取消）
class FakeJtc
{
public:
  enum class Mode { Succeed, NeverFinish };

  FakeJtc(rclcpp::Node::SharedPtr node, Mode mode)
  : mode_(mode)
  {
    srv_ = rclcpp_action::create_server<FollowJointTrajectory>(
      node, "/arm_controller/follow_joint_trajectory",
      [](const rclcpp_action::GoalUUID &, std::shared_ptr<const FollowJointTrajectory::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [this](const std::shared_ptr<ServerGoalHandle> h) {
        cancel_seen_ = true;
        // ⚠️ 这里**不要**调 `h->canceled(...)`：此刻 goal 还在 EXECUTING，rcl_action 的状态机
        //    要求先 EXECUTING → CANCELING 才能 CANCELED，直接调会抛
        //    "invalid transition from state EXECUTING with event CANCELED"。
        //    客户端等的是 **cancel 响应**（`async_cancel_goal` 的 future），所以回 ACCEPT 就够。
        (void)h;
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](const std::shared_ptr<ServerGoalHandle> h) {
        goal_seen_ = true;
        if (mode_ == Mode::NeverFinish) {
          // ⚠️ **必须把 handle 留着**：execute 回调一返回就析构的话，rclcpp_action 会自己把
          //    goal 终止成 CANCELED ⇒ 客户端看到的是"结果回来了"，根本走不到超时分支。
          pending_ = h;
          return;
        }
        auto res = std::make_shared<FollowJointTrajectory::Result>();
        res->error_code = FollowJointTrajectory::Result::SUCCESSFUL;
        h->succeed(res);
      });
  }

  bool goal_seen() const {return goal_seen_;}
  bool cancel_seen() const {return cancel_seen_;}

private:
  Mode mode_;
  rclcpp_action::Server<FollowJointTrajectory>::SharedPtr srv_;
  std::shared_ptr<ServerGoalHandle> pending_;      // NeverFinish 时留住 handle，避免它被析构
  bool goal_seen_ = false;
  bool cancel_seen_ = false;
};

// 假的世界：一个节点 + /joint_states 发布者（**QoS 必须与 joint_state_broadcaster 一致**，陷阱 #58）
struct Fixture
{
  rclcpp::Node::SharedPtr node;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr pub;
  std::vector<double> positions = kStart;
  bool publish = true;

  Fixture()
  {
    static int n = 0;
    node = std::make_shared<rclcpp::Node>("test_motion_client_" + std::to_string(n++));
    auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    pub = node->create_publisher<sensor_msgs::msg::JointState>("joint_states", qos);
  }

  void publish_once()
  {
    if (!publish) {return;}
    sensor_msgs::msg::JointState m;
    m.header.stamp = node->now();
    m.name = kJoints;
    m.position = positions;
    pub->publish(m);
  }

  // 非阻塞地推到 tick() 返回 true；`per_iter` 用来在过程中改变"实测位置"（模拟机械臂动起来）
  void drive(
    arm_motion::MotionClient & mc, const std::function<void()> & per_iter = {},
    double limit_sec = 5.0)
  {
    const auto t0 = std::chrono::steady_clock::now();
    while (rclcpp::ok()) {
      if (per_iter) {per_iter();}
      publish_once();
      rclcpp::spin_some(node);
      if (mc.tick()) {return;}
      if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >
        limit_sec)
      {
        ADD_FAILURE() << "drive 超时：phase=" << mc.phase_name();
        return;
      }
      std::this_thread::sleep_for(5ms);
    }
  }
};

std::string events_text(const arm_motion::MotionClient & mc)
{
  std::string all;
  for (const auto & e : mc.events()) {all += e.text + "\n";}
  return all;
}

}  // namespace

// ① 越界目标 ⇒ 当场拒绝、一帧不发
TEST(MotionClient, RejectsOutOfLimitTargetWithoutSending)
{
  Fixture f;
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  std::vector<double> bad = make_target(5, 99.0);                 // j6 远超限位
  std::string why;
  EXPECT_FALSE(mc.start(bad, &why));
  EXPECT_NE(why.find("越界"), std::string::npos) << why;
  EXPECT_TRUE(mc.events().empty()) << "被拒绝时不该产生事件";
  EXPECT_FALSE(mc.running());
}

// ①b "全 0 目标"也是越界的（j1 的合法区间是 [1.392202, 1.626143]）—— 顺手钉住这条
TEST(MotionClient, RejectsAllZeroTargetBecauseJ1Limits)
{
  Fixture f;
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  std::string why;
  EXPECT_FALSE(mc.start(std::vector<double>(6, 0.0), &why));
  EXPECT_NE(why.find("joint1"), std::string::npos) << why;
}

// ①c 关节顺序/名字不对 ⇒ 也拒绝（限位表是按下标比的）
TEST(MotionClient, RejectsWrongJointNames)
{
  Fixture f;
  std::vector<std::string> wrong = kJoints;
  std::swap(wrong[0], wrong[1]);
  arm_motion::MotionClient mc(f.node.get(), wrong);
  std::string why;
  EXPECT_FALSE(mc.start(kStart, &why));
  EXPECT_NE(why.find("限位表"), std::string::npos) << why;
}

// ② 正常到达：从折叠位动 j2，"机械臂"跟着走 ⇒ ok() 且残差 ≈ 0
TEST(MotionClient, ReachesTargetAndVerifiesResidual)
{
  Fixture f;
  FakeJtc jtc(f.node, FakeJtc::Mode::Succeed);
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  mc.set_tolerance(0.01);
  mc.set_verify_delay(0.05);
  mc.set_state_wait(2.0);
  mc.set_timeout(2.0);

  const std::vector<double> target = make_target(1, -0.2);        // j2: 0.0008 → −0.1992（限位内）
  std::string why;
  ASSERT_TRUE(mc.start(target, &why)) << why;

  bool moved = false;
  f.drive(mc, [&] {
      if (jtc.goal_seen() && !moved) {f.positions = target; moved = true;}   // 收到目标就"动到位"
  }, 5.0);

  EXPECT_TRUE(mc.ok());
  EXPECT_EQ(std::string(mc.phase_name()), "DONE");
  EXPECT_NEAR(mc.max_residual(), 0.0, 1e-9);
  EXPECT_NEAR(mc.travel(), 0.2, 1e-6);
  EXPECT_GT(mc.planned_duration(), 0.0);
  EXPECT_NE(events_text(mc).find("轨迹已接受"), std::string::npos) << events_text(mc);
}

// ③ JTC 报成功但机械臂没动 ⇒ 残差超差判失败（**不能只看 JTC 的返回值**）
TEST(MotionClient, ResidualTooLargeFails)
{
  Fixture f;
  FakeJtc jtc(f.node, FakeJtc::Mode::Succeed);
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  mc.set_tolerance(0.01);
  mc.set_verify_delay(0.05);
  mc.set_state_wait(2.0);
  mc.set_timeout(2.0);

  const std::vector<double> target = make_target(3, 0.5);         // j4: 0.0027 → 0.5027（限位内）
  std::string why;
  ASSERT_TRUE(mc.start(target, &why)) << why;
  f.drive(mc, {}, 5.0);                                          // 位置一直停在折叠位

  EXPECT_FALSE(mc.ok());
  EXPECT_GT(mc.max_residual(), 0.01);
  EXPECT_NE(events_text(mc).find("最大残差"), std::string::npos) << events_text(mc);
}

// ④ 执行超时 ⇒ 真的发出取消并等到确认（陷阱 #60）
TEST(MotionClient, TimesOutAndCancels)
{
  Fixture f;
  FakeJtc jtc(f.node, FakeJtc::Mode::NeverFinish);
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  mc.set_state_wait(2.0);
  mc.set_timeout(0.3);
  mc.set_cancel_wait(2.0);

  std::string why;
  ASSERT_TRUE(mc.start(make_target(1, -0.1), &why)) << why;
  f.drive(mc, {}, 5.0);

  EXPECT_FALSE(mc.ok());
  EXPECT_TRUE(jtc.cancel_seen()) << "超时后**必须**真的取消（否则 JTC 会继续把轨迹走完）";
  EXPECT_TRUE(mc.cancel_confirmed());
  EXPECT_NE(events_text(mc).find("取消已确认"), std::string::npos) << events_text(mc);
}

// ⑤ 没有 action server ⇒ 等不到就报错退出（不无限等）
TEST(MotionClient, ErrorsWhenNoActionServer)
{
  Fixture f;
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  mc.set_state_wait(2.0);
  mc.set_action_wait(0.3);

  std::string why;
  ASSERT_TRUE(mc.start(make_target(1, -0.1), &why)) << why;
  f.drive(mc, {}, 5.0);

  EXPECT_FALSE(mc.ok());
  EXPECT_NE(events_text(mc).find("等不到"), std::string::npos) << events_text(mc);
}

// ⑥ 拿不到 /joint_states ⇒ 拒绝发轨迹（陷阱 #58 的现场表现就是"一帧都收不到"）
TEST(MotionClient, ErrorsWithoutJointStates)
{
  Fixture f;
  f.publish = false;
  arm_motion::MotionClient mc(f.node.get(), kJoints);
  mc.set_state_wait(0.3);

  std::string why;
  ASSERT_TRUE(mc.start(make_target(1, -0.1), &why)) << why;
  f.drive(mc, {}, 5.0);

  EXPECT_FALSE(mc.ok());
  EXPECT_NE(events_text(mc).find("/joint_states"), std::string::npos) << events_text(mc);
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int rc = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return rc;
}
