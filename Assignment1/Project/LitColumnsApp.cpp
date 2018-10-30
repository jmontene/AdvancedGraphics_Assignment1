//***************************************************************************************
// LitColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class LitColumnsApp : public D3DApp
{
public:
    LitColumnsApp(HINSTANCE hInstance);
    LitColumnsApp(const LitColumnsApp& rhs) = delete;
    LitColumnsApp& operator=(const LitColumnsApp& rhs) = delete;
    ~LitColumnsApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	//void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
 
private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 15.0f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        LitColumnsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

LitColumnsApp::LitColumnsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

LitColumnsApp::~LitColumnsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool LitColumnsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
	//BuildSkullGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void LitColumnsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void LitColumnsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void LitColumnsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void LitColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void LitColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LitColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void LitColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void LitColumnsApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void LitColumnsApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void LitColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void LitColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void LitColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void LitColumnsApp::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Create root CBV.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void LitColumnsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void LitColumnsApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.f, 1.f);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.5f, 1.5f, 1.5f, 3);
	GeometryGenerator::MeshData octahedron = geoGen.CreateOctahedron(0.5f);
	GeometryGenerator::MeshData triangularPrism = geoGen.CreateTriangularPrism(1.f, 1.f, 1.f, 3);
	GeometryGenerator::MeshData hexagon = geoGen.CreateHexagon(1.5f, 1.5f, 3);
	GeometryGenerator::MeshData octagon = geoGen.CreateOctagon(1.5f, 1.5f, 3);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.f, 1.f, 0.f, 0.f, 1.f, 3);


	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT diamondVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT wedgeVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT octahedronVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT triPrismVertexOffset = octahedronVertexOffset + (UINT)octahedron.Vertices.size();
	UINT hexagonVertexOffset = triPrismVertexOffset + (UINT)triangularPrism.Vertices.size();
	UINT octagonVertexOffset = hexagonVertexOffset + (UINT)hexagon.Vertices.size();
	UINT coneVertexOffset = octagonVertexOffset + (UINT)octagon.Vertices.size();
	UINT pyramidVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT diamondIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT wedgeIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT octahedronIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT triPrismIndexOffset = octahedronIndexOffset + (UINT)octahedron.Indices32.size();
	UINT hexagonIndexOffset = triPrismIndexOffset + (UINT)triangularPrism.Indices32.size();
	UINT octagonIndexOffset = hexagonIndexOffset + (UINT)hexagon.Indices32.size();
	UINT coneIndexOffset = octagonIndexOffset + (UINT)octagon.Indices32.size();
	UINT pyramidIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry octahedronSubmesh;
	octahedronSubmesh.IndexCount = (UINT)octahedron.Indices32.size();
	octahedronSubmesh.StartIndexLocation = octahedronIndexOffset;
	octahedronSubmesh.BaseVertexLocation = octahedronVertexOffset;

	SubmeshGeometry triPrismSubmesh;
	triPrismSubmesh.IndexCount = (UINT)triangularPrism.Indices32.size();
	triPrismSubmesh.StartIndexLocation = triPrismIndexOffset;
	triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;

	SubmeshGeometry hexagonSubmesh;
	hexagonSubmesh.IndexCount = (UINT)hexagon.Indices32.size();
	hexagonSubmesh.StartIndexLocation = hexagonIndexOffset;
	hexagonSubmesh.BaseVertexLocation = hexagonVertexOffset;

	SubmeshGeometry octagonSubmesh;
	octagonSubmesh.IndexCount = (UINT)octagon.Indices32.size();
	octagonSubmesh.StartIndexLocation = octagonIndexOffset;
	octagonSubmesh.BaseVertexLocation = octagonVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;


	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		diamond.Vertices.size() +
		wedge.Vertices.size() +
		octahedron.Vertices.size() +
		triangularPrism.Vertices.size() +
		hexagon.Vertices.size() +
		octagon.Vertices.size() +
		cone.Vertices.size() +
		pyramid.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
	}

	for (size_t i = 0; i < octahedron.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = octahedron.Vertices[i].Position;
		vertices[k].Normal = octahedron.Vertices[i].Normal;
	}

	for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = triangularPrism.Vertices[i].Position;
		vertices[k].Normal = triangularPrism.Vertices[i].Normal;
	}

	for (size_t i = 0; i < hexagon.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = hexagon.Vertices[i].Position;
		vertices[k].Normal = hexagon.Vertices[i].Normal;
	}

	for (size_t i = 0; i < octagon.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = octagon.Vertices[i].Position;
		vertices[k].Normal = octagon.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(octahedron.GetIndices16()), std::end(octahedron.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangularPrism.GetIndices16()), std::end(triangularPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(hexagon.GetIndices16()), std::end(hexagon.GetIndices16()));
	indices.insert(indices.end(), std::begin(octagon.GetIndices16()), std::end(octagon.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["octahedron"] = octahedronSubmesh;
	geo->DrawArgs["triangularPrism"] = triPrismSubmesh;
	geo->DrawArgs["hexagon"] = hexagonSubmesh;
	geo->DrawArgs["octagon"] = octagonSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

//void LitColumnsApp::BuildSkullGeometry()
//{
//	std::ifstream fin("Models/skull.txt");
//
//	if(!fin)
//	{
//		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
//		return;
//	}
//
//	UINT vcount = 0;
//	UINT tcount = 0;
//	std::string ignore;
//
//	fin >> ignore >> vcount;
//	fin >> ignore >> tcount;
//	fin >> ignore >> ignore >> ignore >> ignore;
//
//	std::vector<Vertex> vertices(vcount);
//	for(UINT i = 0; i < vcount; ++i)
//	{
//		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
//		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
//	}
//
//	fin >> ignore;
//	fin >> ignore;
//	fin >> ignore;
//
//	std::vector<std::int32_t> indices(3 * tcount);
//	for(UINT i = 0; i < tcount; ++i)
//	{
//		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
//	}
//
//	fin.close();
//
//	//
//	// Pack the indices of all the meshes into one index buffer.
//	//
//
//	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
//
//	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);
//
//	auto geo = std::make_unique<MeshGeometry>();
//	geo->Name = "skullGeo";
//
//	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
//	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
//
//	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
//	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
//
//	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
//		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
//
//	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
//		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
//
//	geo->VertexByteStride = sizeof(Vertex);
//	geo->VertexBufferByteSize = vbByteSize;
//	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
//	geo->IndexBufferByteSize = ibByteSize;
//
//	SubmeshGeometry submesh;
//	submesh.IndexCount = (UINT)indices.size();
//	submesh.StartIndexLocation = 0;
//	submesh.BaseVertexLocation = 0;
//
//	geo->DrawArgs["skull"] = submesh;
//
//	mGeometries[geo->Name] = std::move(geo);
//}

void LitColumnsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void LitColumnsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void LitColumnsApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::DimGray);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.2f;

	/*auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 3;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness = 0.3f;*/

	auto wedgeMat = std::make_unique<Material>();
	wedgeMat->Name = "wedgeMat";
	wedgeMat->MatCBIndex = 3;
	wedgeMat->DiffuseSrvHeapIndex = 3;
	wedgeMat->DiffuseAlbedo = XMFLOAT4(.98f, 0.55f, 0.94f, 1.f);
	wedgeMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	wedgeMat->Roughness = 0.3f;

	auto diamondMat = std::make_unique<Material>();
	diamondMat->Name = "diaMat";
	diamondMat->MatCBIndex = 4;
	diamondMat->DiffuseSrvHeapIndex = 4;
	diamondMat->DiffuseAlbedo = XMFLOAT4(0.f, 0.f, 1.f, 1.f);
	diamondMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	diamondMat->Roughness = 0.3f;

	auto octahedronMat = std::make_unique<Material>();
	octahedronMat->Name = "octahedronMat";
	octahedronMat->MatCBIndex = 5;
	octahedronMat->DiffuseSrvHeapIndex = 5;
	octahedronMat->DiffuseAlbedo = XMFLOAT4(1.f, 0.f, 0.f, 1.f);
	octahedronMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	octahedronMat->Roughness = 0.3f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 6;
	sky->DiffuseSrvHeapIndex = 6;
	sky->DiffuseAlbedo = XMFLOAT4(Colors::SkyBlue);
	sky->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	sky->Roughness = 0.3f;

	auto gold = std::make_unique<Material>();
	gold->Name = "gold";
	gold->MatCBIndex = 7;
	gold->DiffuseSrvHeapIndex = 7;
	gold->DiffuseAlbedo = XMFLOAT4(Colors::Goldenrod);
	gold->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	gold->Roughness = 0.2f;

	auto shineBlue = std::make_unique<Material>();
	shineBlue->Name = "shineBlue";
	shineBlue->MatCBIndex = 8;
	shineBlue->DiffuseSrvHeapIndex = 8;
	shineBlue->DiffuseAlbedo = XMFLOAT4(Colors::DeepSkyBlue);
	shineBlue->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	shineBlue->Roughness = 0.05f;

	auto shineRed = std::make_unique<Material>();
	shineRed->Name = "shineRed";
	shineRed->MatCBIndex = 9;
	shineRed->DiffuseSrvHeapIndex = 9;
	shineRed->DiffuseAlbedo = XMFLOAT4(.85f, .2f, .2f, 1.0f);
	shineRed->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	shineRed->Roughness = 0.05f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	//mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["wedgeMat"] = std::move(wedgeMat);
	mMaterials["diaMat"] = std::move(diamondMat);
	mMaterials["octahedronMat"] = std::move(octahedronMat);
	mMaterials["sky"] = std::move(sky);
	mMaterials["gold"] = std::move(gold);
	mMaterials["shineBlue"] = std::move(shineBlue);
	mMaterials["shineRed"] = std::move(shineRed);
}

void LitColumnsApp::BuildRenderItems()
{
	FXMVECTOR AxisX = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	FXMVECTOR AxisY = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	FXMVECTOR AxisZ = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(1.5f, 1.5f, 1.5f)*XMMatrixTranslation(-1.0f, 0.5f, -3.0f));
	XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidRitem->ObjCBIndex = 0;
	pyramidRitem->Mat = mMaterials["wedgeMat"].get();
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));

	auto coneRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(3.f, 2.f, 3.f)*XMMatrixTranslation(0.0f, 7.5f, 6.0f));
	XMStoreFloat4x4(&coneRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneRitem->ObjCBIndex = 1;
	coneRitem->Mat = mMaterials["bricks0"].get();
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	auto cylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&cylinderRitem->World, XMMatrixScaling(5.f, 1.f, 5.f)*XMMatrixTranslation(0.0f, 5.f, 6.0f));
	XMStoreFloat4x4(&cylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	cylinderRitem->ObjCBIndex = 2;
	cylinderRitem->Mat = mMaterials["diaMat"].get();
	cylinderRitem->Geo = mGeometries["shapeGeo"].get();
	cylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(cylinderRitem));

	auto HexagonRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&HexagonRitem->World, XMMatrixScaling(4.5f, 2.0f, 4.5f)*XMMatrixTranslation(0.0f, 2.0f, 6.0f));
	XMStoreFloat4x4(&HexagonRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	HexagonRitem->ObjCBIndex = 3;
	HexagonRitem->Mat = mMaterials["gold"].get();
	HexagonRitem->Geo = mGeometries["shapeGeo"].get();
	HexagonRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	HexagonRitem->IndexCount = HexagonRitem->Geo->DrawArgs["hexagon"].IndexCount;
	HexagonRitem->StartIndexLocation = HexagonRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
	HexagonRitem->BaseVertexLocation = HexagonRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;
	mAllRitems.push_back(std::move(HexagonRitem));

	auto triPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triPrismRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, 0.5f, -8.0f));
	XMStoreFloat4x4(&triPrismRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triPrismRitem->ObjCBIndex = 4;
	triPrismRitem->Mat = mMaterials["sky"].get();
	triPrismRitem->Geo = mGeometries["shapeGeo"].get();
	triPrismRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triPrismRitem->IndexCount = triPrismRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	triPrismRitem->StartIndexLocation = triPrismRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	triPrismRitem->BaseVertexLocation = triPrismRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triPrismRitem));

	auto octahedronRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&octahedronRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, 1.5f, -5.0f));
	XMStoreFloat4x4(&octahedronRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	octahedronRitem->ObjCBIndex = 5;
	octahedronRitem->Mat = mMaterials["octahedronMat"].get();
	octahedronRitem->Geo = mGeometries["shapeGeo"].get();
	octahedronRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	octahedronRitem->IndexCount = octahedronRitem->Geo->DrawArgs["octahedron"].IndexCount;
	octahedronRitem->StartIndexLocation = octahedronRitem->Geo->DrawArgs["octahedron"].StartIndexLocation;
	octahedronRitem->BaseVertexLocation = octahedronRitem->Geo->DrawArgs["octahedron"].BaseVertexLocation;
	mAllRitems.push_back(std::move(octahedronRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(4.5f, 2.0f, 4.5f)*XMMatrixTranslation(0.0f, 0.5f, 6.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 6;
	boxRitem->Mat = mMaterials["stone0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 7;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	/*auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 3;
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	mAllRitems.push_back(std::move(skullRitem));*/

	auto WedgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&WedgeRitem->World, XMMatrixScaling(.5f, .5f, 3.5f)*XMMatrixRotationAxis(AxisY,XMConvertToRadians(-90))*XMMatrixTranslation(0.0f, .5f, -8.0f));
	XMStoreFloat4x4(&WedgeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	WedgeRitem->ObjCBIndex = 8;
	WedgeRitem->Mat = mMaterials["wedgeMat"].get();
	WedgeRitem->Geo = mGeometries["shapeGeo"].get();
	WedgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	WedgeRitem->IndexCount = WedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	WedgeRitem->StartIndexLocation = WedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	WedgeRitem->BaseVertexLocation = WedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(WedgeRitem));

	auto diamondRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem1->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)* XMMatrixTranslation(-0.7f, 2.7f, -7.7f));
	diamondRitem1->TexTransform = MathHelper::Identity4x4();
	diamondRitem1->ObjCBIndex = 9;
	diamondRitem1->Mat = mMaterials["shineBlue"].get();
	diamondRitem1->Geo = mGeometries["shapeGeo"].get();
	diamondRitem1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem1->IndexCount = diamondRitem1->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem1->StartIndexLocation = diamondRitem1->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem1->BaseVertexLocation = diamondRitem1->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem1));

	auto diamondRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem2->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)* XMMatrixTranslation(0.7f, 2.7f, -7.7f));
	diamondRitem2->TexTransform = MathHelper::Identity4x4();
	diamondRitem2->ObjCBIndex = 10;
	diamondRitem2->Mat = mMaterials["diaMat"].get();
	diamondRitem2->Geo = mGeometries["shapeGeo"].get();
	diamondRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem2->IndexCount = diamondRitem2->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem2->StartIndexLocation = diamondRitem2->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem2->BaseVertexLocation = diamondRitem2->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem2));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 3.0f, 1.0f);
	XMMATRIX sphereTransform = XMMatrixScaling(1.4f, 1.4f, 1.4f);
	UINT objCBIndex = 11;
	for(int i = 0; i < 2; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-3.0f, 2.f, 1.5f + i*8.9f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+3.0f, 2.f, 1.5f + i*8.9f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-3.0f, 5.f, 1.5f + i*8.9f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+3.0f, 5.f, 1.5f + i*8.9f);

		XMStoreFloat4x4(&leftCylRitem->World, brickTexTransform * rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["octagon"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["octagon"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["octagon"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, brickTexTransform * leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["octagon"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["octagon"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["octagon"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, sphereTransform*leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, sphereTransform*rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	XMMATRIX hexTransform = XMMatrixScaling(.5f, 1.f, .5f);
	XMMATRIX coneTransform = XMMatrixScaling(1.4f, 1.4f, 1.4f);
	objCBIndex = 19;
	for (int i = 0; i < 2; ++i)
	{
		auto leftHexRitem = std::make_unique<RenderItem>();
		auto righHexRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftHexWorld = XMMatrixTranslation(-7.0f, 2.f, 1.5f + i * 11.f);
		XMMATRIX rightHexWorld = XMMatrixTranslation(+7.0f, 2.f, 1.5f + i * 11.f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-7.0f, 3.f, 1.5f + i * 11.f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+7.0f, 3.f, 1.5f + i * 11.f);

		XMStoreFloat4x4(&leftHexRitem->World, hexTransform*leftHexWorld);
		XMStoreFloat4x4(&leftHexRitem->TexTransform, brickTexTransform);
		leftHexRitem->ObjCBIndex = objCBIndex++;
		leftHexRitem->Mat = mMaterials["diaMat"].get();
		leftHexRitem->Geo = mGeometries["shapeGeo"].get();
		leftHexRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftHexRitem->IndexCount = leftHexRitem->Geo->DrawArgs["hexagon"].IndexCount;
		leftHexRitem->StartIndexLocation = leftHexRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
		leftHexRitem->BaseVertexLocation = leftHexRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;

		XMStoreFloat4x4(&righHexRitem->World, hexTransform*rightHexWorld);
		XMStoreFloat4x4(&righHexRitem->TexTransform, brickTexTransform);
		righHexRitem->ObjCBIndex = objCBIndex++;
		righHexRitem->Mat = mMaterials["diaMat"].get();
		righHexRitem->Geo = mGeometries["shapeGeo"].get();
		righHexRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		righHexRitem->IndexCount = righHexRitem->Geo->DrawArgs["hexagon"].IndexCount;
		righHexRitem->StartIndexLocation = righHexRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
		righHexRitem->BaseVertexLocation = righHexRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["gold"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["cone"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["gold"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["cone"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["cone"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftHexRitem));
		mAllRitems.push_back(std::move(righHexRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	// All the render items are opaque.
	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void LitColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}
