// ROS includes.
#include <ros/ros.h>

// Custom includes
#include "SVHNode.h"
#include <icl_core/EnumHelper.h>



/*--------------------------------------------------------------------
 * Callback functions
 *------------------------------------------------------------------*/

SVHNode::SVHNode(const ros::NodeHandle & nh)
{
  // Initialize the icl_library logging framework
  icl_core::logging::initialize();


  //==========
  // Params
  //==========

  bool autostart;
  int reset_timeout;
  std::vector<bool> disable_flags(driver_svh::eSVH_DIMENSION, false);
  try
  {
    nh.param<bool>("autostart",autostart,false);
    nh.param<std::string>("serial_device",serial_device_name_,"/dev/ttyUSB0");
    // Note : Wrong values (like numerics) in the launch file will lead to a "true" value here
    nh.getParam("disable_flags",disable_flags);
    nh.getParam("reset_timeout",reset_timeout);
  }
  catch (ros::InvalidNameException e)
  {
    ROS_ERROR("Parameter Error!");
  }

  for (size_t i = 0; i < 9; ++i)
  {
    if(disable_flags[i])
    {
      ROS_WARN("svh_controller disabling channel nr %i", i);
    }
  }

  // Init the actual driver hook (after logging initialize)
  fm_.reset(new driver_svh::SVHFingerManager(disable_flags,serial_device_name_,reset_timeout));

  std::vector< std::vector<float> > position_settings(driver_svh::eSVH_DIMENSION);
  std::vector< std::vector<float> > current_settings(driver_svh::eSVH_DIMENSION);


  // get the the indidividual finger params
  // We will read out all of them, so that in case we fail half way we do not set anything
  try
  {
    nh.getParam("THUMB_FLEXION/position_controller",position_settings[driver_svh::eSVH_THUMB_FLEXION]);
    nh.getParam("THUMB_FLEXION/current_controller",current_settings[driver_svh::eSVH_THUMB_FLEXION]);
    nh.getParam("THUMB_OPPOSITION/position_controller",position_settings[driver_svh::eSVH_THUMB_OPPOSITION]);
    nh.getParam("THUMB_OPPOSITION/current_controller",current_settings[driver_svh::eSVH_THUMB_OPPOSITION]);
    nh.getParam("INDEX_FINGER_DISTAL/position_controller",position_settings[driver_svh::eSVH_INDEX_FINGER_DISTAL]);
    nh.getParam("INDEX_FINGER_DISTAL/current_controller",current_settings[driver_svh::eSVH_INDEX_FINGER_DISTAL]);
    nh.getParam("INDEX_FINGER_PROXIMAL/position_controller",position_settings[driver_svh::eSVH_INDEX_FINGER_PROXIMAL]);
    nh.getParam("INDEX_FINGER_PROXIMAL/current_controller",current_settings[driver_svh::eSVH_INDEX_FINGER_PROXIMAL]);
    nh.getParam("MIDDLE_FINGER_DISTAL/position_controller",position_settings[driver_svh::eSVH_MIDDLE_FINGER_DISTAL]);
    nh.getParam("MIDDLE_FINGER_DISTAL/current_controller",current_settings[driver_svh::eSVH_MIDDLE_FINGER_DISTAL]);
    nh.getParam("MIDDLE_FINGER_PROXIMAL/position_controller",position_settings[driver_svh::eSVH_MIDDLE_FINGER_PROXIMAL]);
    nh.getParam("MIDDLE_FINGER_PROXIMAL/current_controller",current_settings[driver_svh::eSVH_MIDDLE_FINGER_PROXIMAL]);
    nh.getParam("RING_FINGER/position_controller",position_settings[driver_svh::eSVH_RING_FINGER]);
    nh.getParam("RING_FINGER/current_controller",current_settings[driver_svh::eSVH_RING_FINGER]);
    nh.getParam("PINKY/position_controller",position_settings[driver_svh::eSVH_PINKY]);
    nh.getParam("PINKY/current_controller",current_settings[driver_svh::eSVH_PINKY]);
    nh.getParam("FINGER_SPREAD/position_controller",position_settings[driver_svh::eSVH_FINGER_SPREAD]);
    nh.getParam("FINGER_SPREAD/current_controller",current_settings[driver_svh::eSVH_FINGER_SPREAD]);

    for (size_t channel = 0; channel < driver_svh::eSVH_DIMENSION; ++channel)
    {
      setCurrentControllerParams(static_cast<driver_svh::SVHChannel>(channel),driver_svh::SVHCurrentSettings(current_settings[channel]));
      setPositionControllerParams(static_cast<driver_svh::SVHChannel>(channel),driver_svh::SVHPositionSettings(position_settings[channel]));
    }
  }
  catch (ros::InvalidNameException e)
  {
    ROS_ERROR("Parameter Error! While reading the controller settings. Will use default settings");
  }

  // prepare the channel position message for later sending
  channel_pos_.name.resize(driver_svh::eSVH_DIMENSION);
  channel_pos_.position.resize(driver_svh::eSVH_DIMENSION, 0.0);
  for (size_t channel = 0; channel < driver_svh::eSVH_DIMENSION; ++channel)
  {
    channel_pos_.name[channel] = driver_svh::SVHController::m_channel_description[channel];
  }

  // Connect and start the reset so that the hand is ready for use
  if (autostart && fm_->connect(serial_device_name_))
  {
    fm_->resetChannel(driver_svh::eSVH_ALL);
    ROS_INFO("Driver was autostarted! Input can now be sent. Have a safe and productive day!");
  }
  else
  {
    ROS_INFO("SVH Driver Ready, you will need to connect and reset the fingers before you can use the hand.");
  }

}

SVHNode::~SVHNode()
{
  // Tell the driver to close connections
  fm_->disconnect();
}

// Callback function for changing parameters dynamically
void SVHNode::dynamic_reconfigure_callback(svh_controller::svhConfig &config, uint32_t level)
{
  serial_device_name_ = config.serial_device;
  setFingerResetSpeed(config.finger_reset_speed);
  fm_->setResetTimeout(config.reset_timeout);
}

// Callback function for connecting to SCHUNK five finger hand
void SVHNode::connectCallback(const std_msgs::Empty&)
{
  if (fm_->isConnected())
  {
    fm_->disconnect();
  }

  if (!fm_->connect(serial_device_name_))
  {
    ROS_ERROR("Could not connect to SCHUNK five finger hand with serial device %s", serial_device_name_.c_str());
  }
}

// Callback function to reset/home channels of SCHUNK five finger hand
void SVHNode::resetChannelCallback(const std_msgs::Int8ConstPtr& channel)
{
  // convert int8 channel into SVHChannel enum
  driver_svh::SVHChannel svh_channel = static_cast<driver_svh::SVHChannel>(channel->data);

  if (fm_->resetChannel(svh_channel))
  {
    ROS_INFO("Channel %i successfully homed!", svh_channel);
  }
  else
  {
    ROS_ERROR("Could not reset channel %i !", svh_channel);
  }
}

// Callback function to enable channels of SCHUNK five finger hand
void SVHNode::enableChannelCallback(const std_msgs::Int8ConstPtr& channel)
{
  fm_->enableChannel(static_cast<driver_svh::SVHChannel>(channel->data));
}

// Callback function for setting channel target positions to SCHUNK five finger hand
void SVHNode::jointStateCallback(const sensor_msgs::JointStateConstPtr& input )
{
  // vector to read target positions from joint states
  std::vector<double> target_positions(driver_svh::eSVH_DIMENSION, 0.0);
  // bool vector storing true, if new target position read
  std::vector<bool> pos_read(driver_svh::eSVH_DIMENSION, false);
  // target positions read counter
  u_int8_t pos_read_counter = 0;

  int32_t index = 0;
  std::vector<std::string>::const_iterator joint_name;
  for (joint_name = input->name.begin(); joint_name != input->name.end(); ++joint_name,++index)
  {
    int32_t channel = 0;
    if (icl_core::string2Enum((*joint_name), channel, driver_svh::SVHController::m_channel_description))
    {
      if (input->position.size() > index)
      {
        target_positions[channel] = input->position[index];
        pos_read[channel] = true;
        pos_read_counter++;
      }
      else
      {
        ROS_WARN("Vector of input joint state is too small! Cannot acces elemnt nr %i", index);
      }
    }
    else
    {
      //ROS_WARN("Could not map joint name %s to channel!", (*joint_name).c_str());
    }
  }

  // send target values at once
  if (pos_read_counter == driver_svh::eSVH_DIMENSION)
  {
    if (!fm_->setAllTargetPositions(target_positions))
    {
      ROS_WARN_ONCE("Set target position command rejected!");
    }
  }
  // not all positions has been set: send only the available positions
  else
  {
    for (size_t i = 0; i < driver_svh::eSVH_DIMENSION; ++i)
    {
      if (pos_read[i])
      {
        fm_->setTargetPosition(static_cast<driver_svh::SVHChannel>(i), target_positions[i], 0.0);
      }
    }
  }
}


sensor_msgs::JointState SVHNode::getCurrentPositions()
{
  if (fm_->isConnected())
  {
    // Get positions in rad
    for (size_t channel = 0; channel < driver_svh::eSVH_DIMENSION; ++channel)
    {
      double cur_pos = 0.0;
      if (fm_->isHomed(static_cast<driver_svh::SVHChannel>(channel)))
      {
        fm_->getPosition(static_cast<driver_svh::SVHChannel>(channel), cur_pos);
      }

      channel_pos_.position[channel] = cur_pos;
    }
  }

  channel_pos_.header.stamp = ros::Time::now();
  return  channel_pos_;
}

void SVHNode::setFingerResetSpeed(const float &resetSpeed)
{
  fm_->setResetSpeed(resetSpeed);
}


// overwrite current parameters
bool SVHNode::setCurrentControllerParams(const driver_svh::SVHChannel &channel, const driver_svh::SVHCurrentSettings &current_settings)
{
   fm_->setCurrentControllerParams(channel,current_settings);
}

// overwrite position parameters
bool SVHNode::setPositionControllerParams(const driver_svh::SVHChannel &channel, const driver_svh::SVHPositionSettings &position_settings)
{
  fm_->setPositionControllerParams(channel, position_settings);
}


/*--------------------------------------------------------------------
 * main()
 * Main function to set up ROS node.
 *------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  //==========
  // ROS
  //==========

  // Set up ROS.
  ros::init(argc, argv, "svh_controller");
  // Private NH for general params
  ros::NodeHandle nh("~");


  // Tell ROS how fast to run this node. (100 = 100 Hz = 10 ms)
  ros::Rate rate(100);

  //==========
  // Logic
  //==========
  // Node object holding all the relevant functions
  SVHNode svh_node(nh);

  //==========
  // Dynamic Reconfigure
  //==========

  dynamic_reconfigure::Server<svh_controller::svhConfig> server;
  dynamic_reconfigure::Server<svh_controller::svhConfig>::CallbackType f;

  f = boost::bind(&SVHNode::dynamic_reconfigure_callback,&svh_node, _1, _2);
  server.setCallback(f);

  //==========
  // Callbacks
  //==========

  // Subscribe connect topic (Empty)
  ros::Subscriber connect_sub = nh.subscribe("connect", 1, &SVHNode::connectCallback,&svh_node);
  // Subscribe reset channel topic (Int8)
  ros::Subscriber reset_sub = nh.subscribe("reset_channel", 1, &SVHNode::resetChannelCallback,&svh_node);
  // Subscribe enable channel topic (Int8)
  ros::Subscriber enable_sub = nh.subscribe("enable_channel", 1, &SVHNode::enableChannelCallback, &svh_node);
  // Subscribe joint state topic
  ros::Subscriber channel_target_sub = nh.subscribe<sensor_msgs::JointState>("channel_targets", 1, &SVHNode::jointStateCallback,&svh_node );
  // Publish current channel positions
  ros::Publisher channel_pos_pub = nh.advertise<sensor_msgs::JointState>("channel_feedback", 1);

  //==========
  // Messaging
  //==========

  // Main loop.
  while (nh.ok())
  {
    // get the current positions of all joints and publish them
    channel_pos_pub.publish(svh_node.getCurrentPositions());

    ros::spinOnce();
    rate.sleep();
  }


  return 0;
}