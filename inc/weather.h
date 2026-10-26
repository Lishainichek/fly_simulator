#ifndef WEATHER_H
#define WEATHER_H
typedef enum {
    WEATHER_GOOD,
    WEATHER_BAD
} WeatherType;

typedef struct {
    WeatherType type;
    int change_interval;
    int last_change;
} Weather;

Weather create_weather(int change_interval);

void weather_tick(Weather *weather, int current_time);

int weather_can_fly(const Weather *weather);

const char *weather_name(const Weather *weather);

#endif