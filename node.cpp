#include "node.h"
#include "v8\libplatform\libplatform.h"

using namespace v8;

static int v8_thread_pool_size = 4;
static bool use_debug_agent = false;
static Isolate* node_isolate;

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





int node::Start(int argc, char * argv[])
{
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
static void StartNodeInstance(void* arg) {
	NodeInstanceData* instance_data = static_cast<NodeInstanceData*>(arg);
	Isolate::CreateParams params;
	ArrayBufferAllocator* array_buffer_allocator = new ArrayBufferAllocator();
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
		Environment* env = CreateEnvironment(isolate,context,instance_data);
	}
}
static Environment* CreateEnvironment(Isolate* isolate,Local<Context> context,NodeInstanceData* instance_data) {
	HandleScope handle_sope(isolate);
	Context::Scope context_scope(context);
	Environment* env = Environment::New(context,instance_data->event_loop());

}
inline Environment* Environment::New(v8::Local<v8::Context> context,uv_loop_t* loop) {
	Environment* env = new Environment(context, loop);

}
inline Environment::Environment(v8::Local<v8::Context> context, uv_loop_t* loop)
	:isolate_(context->)
