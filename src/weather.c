#include "../inc/weather.h"
#include<stdlib.h>
Weather create_weather(int change_interval) {
    Weather weather;

    weather.type = WEATHER_GOOD;
    weather.change_interval = change_interval;
    weather.last_change = 0;

    return weather;
}

void weather_tick(Weather *weather, int current_time) {
    if (weather == NULL || weather->change_interval <= 0) {
        return;
    }

    if (current_time - weather->last_change <
        weather->change_interval) {
        return;
    }

    if (weather->type == WEATHER_GOOD) {
        weather->type = WEATHER_BAD;
    } else {
        weather->type = WEATHER_GOOD;
    }

    weather->last_change = current_time;
}

int weather_can_fly(const Weather *weather) {
    if (weather == NULL) {
        return 0;
    }

    return weather->type == WEATHER_GOOD;
}

const char *weather_name(const Weather *weather) {
    if (weather == NULL) {
        return "UNKNOWN";
    }

    if (weather->type == WEATHER_GOOD) {
        return "GOOD";
    }

    return "BAD";
}