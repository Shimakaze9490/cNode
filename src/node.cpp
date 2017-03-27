#include "node.h"
#include "libplatform\libplatform.h"
#include "src\handles.h"
#include "src\api.h"
#include "env.h"
#include "env-inl.h"
#include "node-internals.h"

using namespace v8;

static int v8_thread_pool_size = 4;
static bool use_debug_agent = false;
static v8::Isolate* node_isolate;
static const int kContextEmbedderDataIndex = 32;//NODE_CONTEXT_EMBEDDER_DATA_INDEX;


static void HandleCloseCb(uv_handle_t* handle) {
	Environment* env = reinterpret_cast<Environment*>(handle->data);
	env->FinishHandleCleanup(handle);
}

static void HandleCleanup(Environment* env,
	uv_handle_t* handle,
	void* arg) {
	handle->data = env;
	uv_close(handle, HandleCloseCb);
}

static struct 
{
	void Initialize(int thread_pool_size) {
		//CreateDefaultPlatform��һ���麯������д������ʼ����
		platform_ = v8::platform::CreateDefaultPlatform(thread_pool_size);
		V8::InitializePlatform(platform_);
	}

	void Dispose() {
		delete platform_;
		platform_ = nullptr;
	}

	Platform* platform_;
}v8_platform;

static void StartNodeInstance(void* arg) {
	node::NodeInstanceData* instance_data = static_cast<node::NodeInstanceData*>(arg);
	Isolate::CreateParams params;
	node::ArrayBufferAllocator* array_buffer_allocator = new node::ArrayBufferAllocator();
	params.array_buffer_allocator = array_buffer_allocator;

	Isolate* isolate = Isolate::New(params);
	{
		//Mutex::ScopedLock scoped_lock();
		if (instance_data->is_main()) {
			node_isolate = isolate;
		}
	}
	{
		/* ��V8�У��ڴ���䶼����V8��Heap�н��з���ģ�JavaScript��ֵ�Ͷ���Ҳ�������V8��Heap�С�
		���Heap��V8������ȥά����ʧȥ���õĶ��󽫻ᱻV8��GC�����������·�����������󡣶�Handle���Ƕ�Heap�ж�������á�
		V8Ϊ�˶��ڴ������й���GC��Ҫ��V8�е����ж�����и��٣�����������Handle��ʽ���õģ�����GC��Ҫ��Handle���й���
		����GC����֪��Heap��һ������������������һ�������Handle����Ϊ�����ı��ʱ��GC���ɶԸö�����л��գ�gc�������ƶ���
		��ˣ�V8����б���ʹ��Handleȥ����һ�����󣬶�����ֱ��ͨ��C++�ķ�ʽȥ��ȡ��������ã�ֱ��ͨ��C++�ķ�ʽȥֱ��ȥ����һ������
		��ʹ�øö����޷���V8����*/
		/*һ�������У������кܶ�Handle����HandleScope���൱������װHandle��Local������������HandleScope�������ڽ�����ʱ��
		HandleҲ���ᱻ�ͷţ�������Heap�ж������õĸ��¡�HandleScope�Ƿ�����ջ�ϣ�����ͨ��New�ķ�ʽ���д�����
		����ͬһ���������ڿ����ж��HandleScope���µ�HandleScope���Ḳ����һ��HandleScope������Local Handle���й���*/
		Locker locker(isolate);
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_sope(isolate);
		Local<Context> context = Context::New(isolate);
		Environment* env = CreateEnvironment(isolate, context, instance_data);
	}
}

void SetupProcessObject(Environment* env, int argc, const char* const* argv, int exec_argc, const char* const* exec_argv) {
	HandleScope scope(env->isolate());
	Local<Object> process = env->process_object();
	auto title_string = node::OneByteString(env->isolate(), "title", sizeof("title") - 1);
	env->context();
	//process.version
	READONLY_PROPERTY(process, "version", FIXED_ONE_BYTE_STRING(env->isolate(), "cNode1.0~"));
	//process.moduleLoadList
	READONLY_PROPERTY(process, "moduleLoadList", env->module_load_list_array());
}

static Environment* CreateEnvironment(Isolate* isolate, Local<Context> context, node::NodeInstanceData* instance_data) {
	HandleScope handle_sope(isolate);
	Context::Scope context_scope(context);
	Environment* env = Environment::New(context, instance_data->event_loop());
	isolate->SetAutorunMicrotasks(false);
	uv_check_init(env->event_loop(), env->immediate_check_handle());
	uv_unref(
		reinterpret_cast<uv_handle_t*>(env->immediate_check_handle()));

	uv_idle_init(env->event_loop(), env->immediate_idle_handle());
	//��cpu���е�ʱ��֪ͨv8�ĵ�CPU����������¼��epoll_wait()��ʱ����ʹ�������߿����ҵ�����
	uv_prepare_init(env->event_loop(), env->idle_prepare_handle());
	uv_check_init(env->event_loop(), env->idle_check_handle());
	uv_unref(reinterpret_cast<uv_handle_t*>(env->idle_prepare_handle()));
	uv_unref(reinterpret_cast<uv_handle_t*>(env->idle_check_handle()));

	//ע��handle�Ļ��շ�����
	env->RegisterHandleCleanup(
		reinterpret_cast<uv_handle_t*>(env->immediate_check_handle()),
		HandleCleanup,
		nullptr);
	env->RegisterHandleCleanup(
		reinterpret_cast<uv_handle_t*>(env->immediate_idle_handle()),
		HandleCleanup,
		nullptr);
	env->RegisterHandleCleanup(
		reinterpret_cast<uv_handle_t*>(env->idle_prepare_handle()),
		HandleCleanup,
		nullptr);
	env->RegisterHandleCleanup(
		reinterpret_cast<uv_handle_t*>(env->idle_check_handle()),
		HandleCleanup,
		nullptr);

	Local<FunctionTemplate> process_template = FunctionTemplate::New(isolate);
	process_template->SetClassName(node::OneByteString(isolate, "process", sizeof("process") - 1));

	Local<Object> process_object = process_template->GetFunction()->NewInstance(context).ToLocalChecked();
	env->set_process_object(process_object);

	SetupProcessObject(env, instance_data->argc(), instance_data->argv(), instance_data->exec_argc(), instance_data->exec_argv());

}

int node::Start(int argc, char * argv[])
{
	return 0;
	int exec_argc;
	int exit_code = 1;
	const char** exec_argv;
	v8_platform.Initialize(v8_thread_pool_size);
	V8::Initialize();
	{
		NodeInstanceData instance_data(NodeInstanceType::MAIN,uv_default_loop(),argc,const_cast<const char**>(argv),exec_argc, exec_argv, use_debug_agent);
		StartNodeInstance(&instance_data);
		exit_code = instance_data.exit_code();
	}
	V8::Dispose();
	v8_platform.Dispose();
	delete[] exec_argv;
	exec_argv = nullptr;
	return exit_code;
}




	
