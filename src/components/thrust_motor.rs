use bevy::math::Vec3;
use bevy::prelude::Component;

#[derive(Component)]
pub struct ThrustMotor{
    pub thrust: f32,
    pub torque: f32,
    pub offset: Vec3,
}

impl Default for ThrustMotor{
    fn default() -> Self{
        ThrustMotor{
            thrust: 100.0,
            torque: 10.0,
            offset: Vec3::ZERO,
        }
    }
}