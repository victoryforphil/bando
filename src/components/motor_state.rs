use bevy::prelude::Component;

#[derive(Component)]
pub struct MotorState{
    pub input: f32,
    pub current: f32,
}

impl Default for MotorState{
    fn default() -> Self{
        MotorState{
            input: 0.0,
            current: 0.0,
        }
    }
}