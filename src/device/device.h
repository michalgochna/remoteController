

class Device {
    public:
        Device();
        ~Device();

        void setPosition(float newPosition);
        void homeAxis();
        bool isHomed();
        float getPosition();
        
        float getLimit();
        float setLimit();
        


    private:
        bool homed = false;
        float limit = 1000;
        float currentPosition = 0.0;
        float ratio = 1.0;
        float accel = 1.0;
};