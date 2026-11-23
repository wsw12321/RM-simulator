class_name RMUCVisualMaterials
extends RefCounted

const RED := Color("#ff5b6e")
const BLUE := Color("#5aa8ff")
const AMBER := Color("#ffc56b")
const CYAN := Color("#59e6d5")
const VIOLET := Color("#a98cff")
const INK := Color("#0c1220")
const PANEL := Color("#111b2e")
const STEEL := Color("#445269")

static func metal(color: Color, roughness := 0.34, emission_strength := 0.0) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = 0.72
	material.roughness = roughness
	if emission_strength > 0.0:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = emission_strength
	return material

static func glass(color: Color, alpha := 0.38) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = Color(color.r, color.g, color.b, alpha)
	material.metallic = 0.12
	material.roughness = 0.12
	material.emission_enabled = true
	material.emission = color
	material.emission_energy_multiplier = 1.8
	return material

static func line(color: Color, width := 0.035) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color
	material.emission_energy_multiplier = 4.0
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	return material

static func text_style(color: Color, size := 16) -> LabelSettings:
	var settings := LabelSettings.new()
	settings.font_size = size
	settings.font_color = color
	settings.outline_size = 6
	settings.outline_color = Color(0.02, 0.04, 0.08, 0.9)
	return settings
