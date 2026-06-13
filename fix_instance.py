import sys

filename = '/home/cris/Documents/anoptic-engine/src/vulkan_backend/instance/instanceInit.c'
with open(filename, 'r') as f:
    lines = f.readlines()

in_func = False
func_name = ''

for i, line in enumerate(lines):
    if line.startswith('void cleanupSwapChain('):
        in_func = True
        func_name = 'cleanupSwapChain'
    elif line.startswith('void recreateSwapChain('):
        in_func = True
        func_name = 'recreateSwapChain'
    elif line.startswith('void cleanupVulkan('):
        in_func = True
        func_name = 'cleanupVulkan'
    elif line.startswith('bool createDescriptorSets('):
        in_func = True
        func_name = 'createDescriptorSets'
        
    if in_func:
        if func_name in ['cleanupSwapChain', 'recreateSwapChain', 'cleanupVulkan']:
            lines[i] = lines[i].replace('state->', 'rendererState.')
            if 'createDepthResources(components)' in lines[i]:
                lines[i] = lines[i].replace('createDepthResources(components)', 'createDepthResources(components, &rendererState)')
        
        if func_name == 'createDescriptorSets':
            # Fix the broken string literals and braces
            if '"Failed to allocate global descriptor sets!' in lines[i]:
                lines[i] = '        printf("Failed to allocate global descriptor sets!\\n");\n'
            elif '"Failed to allocate cull descriptor sets!' in lines[i]:
                lines[i] = '        printf("Failed to allocate cull descriptor sets!\\n");\n'
            elif '");' in lines[i]:
                lines[i] = '' # Remove the broken line
    
    if line.startswith('}'):
        in_func = False

with open(filename, 'w') as f:
    f.writelines(lines)
