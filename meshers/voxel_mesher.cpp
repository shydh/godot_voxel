#include "voxel_mesher.h"
#include "../constants/voxel_string_names.h"
#include "../generators/voxel_generator.h"
#include "../storage/voxel_buffer_gd.h"
#include "../util/godot/classes/array_mesh.h"
#include "../util/godot/classes/mesh.h"
#include "../util/godot/classes/shader_material.h"
#include "../util/profiling.h"
#include "transvoxel/transvoxel_cell_iterator.h"

#ifdef VOXEL_ENABLE_SMOOTH_MESHING
#include "../meshers/transvoxel/voxel_mesher_transvoxel.h"
#include "../engine/detail_rendering/detail_rendering.h"
#endif

using namespace zylann::godot;

namespace zylann::voxel {

Ref<Mesh> VoxelMesher::build_mesh(
		const VoxelBuffer &voxels,
		TypedArray<Material> materials,
		Dictionary additional_data
) {
	ZN_PROFILE_SCOPE();

	Output output;
	Input input{ voxels, nullptr, Vector3i(), 0, false, false, false };

#ifdef VOXEL_ENABLE_SMOOTH_MESHING
	DetailRenderingSettings detail_texture_settings;
	detail_texture_settings.begin_lod_index = 0;
#endif

	if (additional_data.size() > 0) {
		// This is mainly for testing purposes, or small-scale meshing.
		Ref<VoxelGenerator> generator = additional_data.get("generator", Variant());
		input.generator = generator.ptr();
		input.origin_in_voxels = additional_data.get("origin_in_voxels", Vector3i());
#ifdef VOXEL_ENABLE_SMOOTH_MESHING
		detail_texture_settings.enabled = additional_data.get("normalmap_enabled", false);
		detail_texture_settings.octahedral_encoding_enabled =
				additional_data.get("octahedral_normal_encoding_enabled", false);
		detail_texture_settings.tile_resolution_min = int(additional_data.get("normalmap_tile_resolution", 16));
		detail_texture_settings.tile_resolution_max = detail_texture_settings.tile_resolution_min;
		detail_texture_settings.max_deviation_degrees = math::clamp(
				int(additional_data.get("normalmap_max_deviation_degrees", 0)),
				int(DetailRenderingSettings::MIN_DEVIATION_DEGREES),
				int(DetailRenderingSettings::MAX_DEVIATION_DEGREES)
		);
		input.detail_texture_hint = detail_texture_settings.enabled;
#endif
	}

	build(output, input);

	if (is_mesh_empty(output.surfaces)) {
		return Ref<ArrayMesh>();
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();

	int gd_surface_index = 0;
	for (unsigned int i = 0; i < output.surfaces.size(); ++i) {
		Output::Surface &surface = output.surfaces[i];
		Array &arrays = surface.arrays;

		if (arrays.is_empty()) {
			continue;
		}

		CRASH_COND(arrays.size() != Mesh::ARRAY_MAX);
		if (!is_surface_triangulated(arrays)) {
			continue;
		}

		Ref<Material> material;
		if (int(surface.material_index) < materials.size()) {
			material = materials[surface.material_index];
		}
		if (material.is_null()) {
			material = get_material_by_index(surface.material_index);
		}

		mesh->add_surface_from_arrays(output.primitive_type, arrays, Array(), Dictionary(), output.mesh_flags);
		mesh->surface_set_material(gd_surface_index, material);
		++gd_surface_index;
	}

#ifdef VOXEL_ENABLE_SMOOTH_MESHING
	if (detail_texture_settings.enabled && input.generator != nullptr) {
		VoxelMesherTransvoxel *transvoxel_mesher = Object::cast_to<VoxelMesherTransvoxel>(this);

		if (transvoxel_mesher != nullptr) {
			const transvoxel::MeshArrays &mesh_arrays = VoxelMesherTransvoxel::get_mesh_cache_from_current_thread();
			Span<const transvoxel::CellInfo> cell_infos = VoxelMesherTransvoxel::get_cell_info_from_current_thread();
			TransvoxelCellIterator cell_iterator(cell_infos);
			DetailTextureData nm_data;

			compute_detail_texture_data(
					cell_iterator,
					to_span(mesh_arrays.vertices),
					to_span(mesh_arrays.normals),
					to_span(mesh_arrays.indices),
					nm_data,
					detail_texture_settings.tile_resolution_min,
					*input.generator,
					nullptr,
					input.origin_in_voxels,
					input.voxels.get_size(),
					input.lod_index,
					detail_texture_settings.octahedral_encoding_enabled,
					math::deg_to_rad(float(detail_texture_settings.max_deviation_degrees)),
					false
			);

			const Vector3i block_size =
					input.voxels.get_size() - Vector3iUtil::create(get_minimum_padding() + get_maximum_padding());

			DetailImages images = store_normalmap_data_to_images(
					nm_data,
					detail_texture_settings.tile_resolution_min,
					block_size,
					detail_texture_settings.octahedral_encoding_enabled
			);

			const DetailTextures textures = store_normalmap_data_to_textures(images);
			// That should be in return value, but for now I just want this for testing with GDScript, so it gotta go
			// somewhere
			mesh->set_meta(VoxelStringNames::get_singleton().voxel_normalmap_atlas, textures.atlas);
			mesh->set_meta(VoxelStringNames::get_singleton().voxel_normalmap_lookup, textures.lookup);
		}
	}
#endif

	return mesh;
}

void VoxelMesher::build(Output &output, const Input &input) {
	ERR_PRINT("Not implemented");
}

unsigned int VoxelMesher::get_minimum_padding() const {
	return _minimum_padding;
}

unsigned int VoxelMesher::get_maximum_padding() const {
	return _maximum_padding;
}

void VoxelMesher::set_padding(int minimum, int maximum) {
	CRASH_COND(minimum < 0);
	CRASH_COND(maximum < 0);
	_minimum_padding = minimum;
	_maximum_padding = maximum;
}

Ref<Material> VoxelMesher::get_material_by_index(unsigned int i) const {
	// May be implemented in some meshers
	return Ref<Material>();
}

unsigned int VoxelMesher::get_material_index_count() const {
	// May be implemented in some meshers
	return 0;
}

bool VoxelMesher::is_mesh_empty(const StdVector<Output::Surface> &surfaces) {
	if (surfaces.size() == 0) {
		return true;
	}
	for (const Output::Surface &surface : surfaces) {
		if (is_surface_triangulated(surface.arrays)) {
			return false;
		}
	}
	return true;
}

Ref<ShaderMaterial> VoxelMesher::get_default_lod_material() const {
	return Ref<ShaderMaterial>();
}

Ref<Mesh> VoxelMesher::_b_build_mesh(
		Ref<godot::VoxelBuffer> voxels,
		TypedArray<Material> materials,
		Dictionary additional_data
) {
	ERR_FAIL_COND_V(voxels.is_null(), Ref<ArrayMesh>());
	return build_mesh(voxels->get_buffer(), materials, additional_data);
}

void VoxelMesher::_bind_methods() {
	// Shortcut if you want to generate a mesh directly from a fixed grid of voxels.
	// Useful for testing the different meshers.
	// TODO Have an object type to specify input
	ClassDB::bind_method(
			D_METHOD("build_mesh", "voxel_buffer", "materials", "additional_data"),
			&VoxelMesher::_b_build_mesh,
			DEFVAL(Dictionary())
	);
	ClassDB::bind_method(D_METHOD("get_minimum_padding"), &VoxelMesher::get_minimum_padding);
	ClassDB::bind_method(D_METHOD("get_maximum_padding"), &VoxelMesher::get_maximum_padding);
}

} // namespace zylann::voxel
