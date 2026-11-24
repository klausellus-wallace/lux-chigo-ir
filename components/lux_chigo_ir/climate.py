"""ESPHome codegen for lux_chigo_ir climate component.

Modeled after esphome/components/coolix/climate.py plus:
- Extra optional Sensor output `ifeel_output` for RX mirroring of the
  I-FEEL room-temperature value the original remote transmits.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir, sensor

AUTO_LOAD = ["climate_ir"]

lux_chigo_ir_ns = cg.esphome_ns.namespace("lux_chigo_ir")
LuxChigoIRClimate = lux_chigo_ir_ns.class_("LuxChigoIRClimate", climate_ir.ClimateIR)

CONF_IFEEL_OUTPUT = "ifeel_output"
CONF_IFEEL = "ifeel"
CONF_IFEEL_INTERVAL = "ifeel_interval"
CONF_IFEEL_SENT_OUTPUT = "ifeel_sent_output"

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(LuxChigoIRClimate).extend(
    {
        cv.Optional(CONF_IFEEL_OUTPUT): cv.use_id(sensor.Sensor),
        # false: `sensor:` only feeds current_temperature; the AC keeps
        # regulating on its own built-in sensor (no I-FEEL in TX frames).
        cv.Optional(CONF_IFEEL, default=True): cv.boolean,
        # Resend the state with I-FEEL every N minutes (HA connected, AC on).
        # Omitted = no timed sends.
        cv.Optional(CONF_IFEEL_INTERVAL): cv.All(
            cv.positive_time_period_seconds,
            cv.Range(min=cv.TimePeriod(seconds=10), max=cv.TimePeriod(minutes=120)),
        ),
        # Last I-FEEL temperature actually transmitted.
        cv.Optional(CONF_IFEEL_SENT_OUTPUT): cv.use_id(sensor.Sensor),
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_ifeel_enabled(config[CONF_IFEEL]))
    if CONF_IFEEL_INTERVAL in config:
        cg.add(var.set_ifeel_interval_s(config[CONF_IFEEL_INTERVAL].total_seconds))
    if CONF_IFEEL_SENT_OUTPUT in config:
        sens = await cg.get_variable(config[CONF_IFEEL_SENT_OUTPUT])
        cg.add(var.set_ifeel_sent_output(sens))
    if CONF_IFEEL_OUTPUT in config:
        sens = await cg.get_variable(config[CONF_IFEEL_OUTPUT])
        cg.add(var.set_ifeel_output(sens))
