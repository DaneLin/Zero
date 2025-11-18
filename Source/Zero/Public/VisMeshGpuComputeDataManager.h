#pragma once


class FVisMeshGpuComputeDispatchInterface;

// Abstract class for managing GPU data for VisMesh
// Once the manager is created it will last for the lifetime of the owner dispatch interface
// The manager will be destroyed when the dispatch interface is also destoryed
class FVisMeshGpuComputeDataManager
{
	UE_NONCOPYABLE(FVisMeshGpuComputeDataManager);

public:

	FVisMeshGpuComputeDataManager(FVisMeshGpuComputeDispatchInterface* InOwnerInterface)
		: InternalOwnerInterface(InOwnerInterface)
	{
		
	}
	virtual ~FVisMeshGpuComputeDataManager();

	FVisMeshGpuComputeDispatchInterface* GetOwnerInterface() const {return InternalOwnerInterface;}
	
private:
	FVisMeshGpuComputeDispatchInterface* InternalOwnerInterface = nullptr;
};