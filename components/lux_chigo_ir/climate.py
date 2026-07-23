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

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(LuxChigoIRClimate).extend(
    {
        cv.Optional(CONF_IFEEL_OUTPUT): cv.use_id(sensor.Sensor),
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    if CONF_IFEEL_OUTPUT in config:
        sens = await cg.get_variable(config[CONF_IFEEL_OUTPUT])
        cg.add(var.set_ifeel_output(sens))
