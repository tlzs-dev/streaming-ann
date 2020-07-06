# Samples
# ケース1：　画像分析、オブジェクト認識
## Overview
	+ Plugins: libaiengine-darknet.so
	+ 適用シナリオ: クライアトサーバーモデル
	+ フロントエンド（ブラウザ）：
		+ カメラで取得したビデオストリームを分析のためにバックグラウンドAIエンジンに送信し、
		+ 開発言語: JavaScript + HTML
	+ バックエンド（AIエンジン）：Webサーバー
		+ オブジェクト認識は、結果がjson形式でフロントエンドに返します。 
		+ 開発言語: C


## Descriptions:
### フロントエンド（ブラウザ）： 開発言語JavaScript
	下记List1, 2 是前端与AI处理和显示相关的全部代码。

```
	
	■list 1: Send HTTP Request (POST method)
	// 関数：　send_request(url, blob, on_draw, canvas, frame);
	// 功能：　以HTTP POST方式，向AI服务器提交解析请求，并将AI解析的结果在界面上显示
	// Parameters：
		url: AI server url
		blob: image data, support binary data ：content-type="image/jpeg" or "image/png," data={binary_data} 
				or json object content-type="application/json" data='{ "image": "[content-type];base64,b64string" }
				
		on_draw: UI renderering function, 实际指向的是draw_detections函数
		canvas: renderering destination
		frame: origin image, HTML <img> object for renderering 
			(用javascript直接处理二进制数据比较麻烦，此处提供同一图像的易于处理的格式）

	function send_request(url, blob, on_draw, canvas, frame)
	{
		if(!blob) return;
		var http = new XMLHttpRequest();	// 
		http.open('POST', url, true);
		http.setRequestHeader('Content-Type', blob.type);

		http.onreadystatechange = function() {
		//	console.log("readyState: " + http.readyState + "status: " + http.status);
			if(http.readyState == 4 && http.status == 200) {
			var result = JSON.parse(http.responseText);
				//   console.log("result: " + JSON.stringify(result));
				on_draw(canvas, frame, result);
			}
		}
		http.send(blob);
	}

	■list 2: 検知結果をRenderingする (show results)

	/*
	 * function draw_detections(canvas, src, result)
	 * Parameters: 
	 * 	canvas:	dst canvas
	 *  src:	origin image  ( <img> | <video> )
	 * 	result: json_result, 格式如下
	 * 	{
			"detections": [
				{ "x": 0.05, "y": 0.1, "width": 0.3, "height": 0.1 },
				{ "x": 0.35, "y": 0.4, "width": 0.15, "height": 0.31 }
			]
		}
	 * @TODO: set src = current frame
	*/

	function draw_detections(canvas, src, result)
	{
		var cr = canvas.getContext('2d');
		if(src)	// 绘制原始图像
		{
			cr.drawImage(src, 0, 0, canvas.width, canvas.height)
		}
		if(! result) return;	// 如果没有解析结果则不做任何处理
		
		// 绘制解析结果
		cr.strokeStyle = "#ffff00";		// 矩形框线的颜色（黄色）
		cr.lineWidth = 2;				// 线宽

		var image_width = canvas.width;		// 显示区域的宽度 (viewport.width )
		var image_height = canvas.height;	// 显示区域的高度 (viewport.height)
		
		var detections = result['detections'];	// AI识别出的所有物体的集合
		cr.beginPath();	// 准备绘制结果 （清除上一次绘制的结果）
		for(var i = 0; i < detections.length; ++i)	// 遍历所有识别出的物体
		{
			var det = detections[i];	// 当前的物体
			// 获取当前物体所在的坐标和大小，并将相对坐标转换为视口坐标
			var x = det['left'] * image_width;	
			var y = det['top'] * image_height;
			var cx = det['width'] * image_width;
			var cy = det['height'] * image_height;

			cr.rect(x, y, cx, cy);		// 用一个矩形框绘制物体所在的位置
			cr.font = "20px Arial";		// 设置显示字体和字号
			cr.fillStyle = "blue";		// 设置字体颜色
			cr.fillText(det['class'], x, y);	// 在左上角显示物体的标签（名称）
		}
		cr.stroke();	// 完成此次绘制
	}

```

### バックエンド（AIエンジン）：開発言語C

```

	■list 1: WebServer的配置文件(全部配置)
	{
		"port": 8081,	// http服务器的侦听端口
		"cert-file": "ssl/certs/api.tlzs.co.jp.cert.pem", 	// HTTPS 服务器证书，由于此处使用的是个人签发的证书，浏览器访问网站时会有安全警告，忽略即可
		"key-file":	"ssl/keys/api.tlzs.co.jp.key.pem",		// HTTP证书密钥
		"input": {		// IO模块，在此示例中实际未使用，各个模块是完全独立的，彼此间没有依赖，可自由组合
			"type": "io-plugin::httpd",
			"port": "9002",
		},
		"engine": { // AI engine的配置，使用YoloV3模型来识别物体
			"type": "ai-engine::darknet",			// 使用的AI框架
			"conf_file": "models/yolov3.cfg",		// YoloV3模型的定义文件， 可用自定义的配置文件直接替换
			"weights_file": "models/yolov3.weights",// YoloV模型预训练的权重数据，可用自己训练的数据直接替换
		},
	}

	■list 2
	初始化AIengine并启动web服务
	// step 1. 加载"plugins"目录下的所有可用的plugin
		ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, "plugins", NULL); 
		
	// step2. 启动AIengine
		start_ai_engine();

	// step3. 初始化HTTP服务器	
		SoupServer * server = soup_server_new(
			SOUP_SERVER_SERVER_HEADER, "StreammingAnn-Demo", 
			SOUP_SERVER_SSL_CERT_FILE, "ssl/certs/api.tlzs.co.jp.cert.pem",
			SOUP_SERVER_SSL_KEY_FILE, "ssl/keys/api.tlzs.co.jp.key.pem",
			NULL);
		ctx->server = server;
		
		// 白名单方式，限制网站只使用下面这一个URL路径
		soup_server_add_handler(server, "/tlzs/demo", on_demo_handler, ctx, NULL);	
		soup_server_listen_all(server, 
			8081,	// 为简化代码，不读取配置文件，直接硬编码写入侦听端口
			SOUP_SERVER_LISTEN_HTTPS, 			// 强制使用HTTPS方式，因为HTML5的mediaDevice类要求只有在HTTPS方式下才能使用本地摄像头。
			&gerr);
		
		GMainLoop * loop = g_main_loop_new(NULL, FALSE);	// 使用signal/event（消息机制）来处理Web请求 
		g_main_loop_run(loop);	// 启动message loop


	■list 3. 启动AIengine的具体实现(为简化说明，下面的清单中忽略了所有异常处理和防御性代码)
	int start_ai_engine()
	{
		webserver_context_t * ctx = g_context; // 定义一个全局context, 用于支持各模块间的联动
		json_object * jconfig = json_object_from_file("conf/webserver.json");	// 加载全局配置文件
		json_object * jengine = NULL;
		json_bool ok = FALSE;
		
		// 获取全局配置中AIengine的定义
		ok = json_object_object_get_ex(jconfig, "engine", &jengine); assert(ok && jengine); 
		
		// 获取AIengine使用的plugin类型
		const char * engine_type = json_get_value(jengine, string, type);
		
		// 生成一个AIengine对象
		ai_engine_t * engine = ai_engine_init(NULL, engine_type, ctx);;
		
		// 加载AIengine专用的配置
		rc = engine->init(engine, jengine);	assert(0 == rc);

		ctx->engine = engine;	// 让别的模块能从全局context中快速找到AIengine
		return rc;
	}


	■list 4 处理Web请求
	static void on_demo_handler(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
	{
		// 由于白名单限制了此示例中只可能有唯一的访问路径，无需对path作额外判断。
		
		if(msg->method == SOUP_METHOD_GET)	
		{
			// 如果请求是GET方法，则显示当前主页（内含此示例中的全部HTML和javascript代码）
			on_get_homepage(server, msg, path, query, client, user_data);
			return;
		}else if(msg->method == SOUP_METHOD_POST || msg->method == SOUP_METHOD_PUT)	// 
		{
			// 如果是POST方法，使用AIengine来解析客户端传来的图像，并返回json结果。
			on_ai_request(server, msg, path, query, client, user_data);
			return;
		}else if(msg->method == SOUP_METHOD_HEAD)
		{
			// 此示例中不使用该方法，仅供用CURL工具测试网站联通性时使用
			soup_message_set_status(msg, SOUP_STATUS_OK);	// 
			return;
		}else if(msg->method == SOUP_METHOD_OPTION)
		{
			// TODO: add CORS support, 
			// 出于安全考量，当前的主流浏览器通常都会限制javascript的跨站脚本访问，
			// 由于此示例中AIengine直接继承在web服务器内部，无需跨站（跨域名）访问，故此省略相关处理
			// 如有必要，可在此处添加允许CORS的相关headers即可。
		}
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}

	■list 5.
	static void on_ai_request(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
	{
		static const char * fmt = "{\"error_code\": %d, \"error_msg\": \"%s\" }\r\n";
		
		// 获取POSTdata的数据格式， 为简化说明，下面的代码中仅处理image/jpeg一种格式。
		const char * req_content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
		
		if(g_content_type_equals(req_content_type, "application/json"))
		{
			// TODO‘
		}else if(g_content_type_equals(req_content_type, "image/jpeg"))
		{
			input_frame_t * frame = input_frame_new();
			SoupMessageBody * body = msg->request_body;
			
			// 加入文件大小限制，下面的assert不应在这一场合使用，正式代码中需要在判断后返回错误码，
			// 此处由于客户端代码已经预先测试并硬编码写死，不会出问题。
			// 此处的assert仅用于简化说明
			assert(body && body->data && body->length > 0 && body->length <= (10 * 1000 * 1000)); 
			
			// 此示例中没有使用IO模块，直接使用用户上传的原始数据。
			input_frame_set_jpeg(frame, (unsigned char *)body->data, body->length, NULL, 0);
			
			webserver_context_t * ctx = user_data;
			ai_engine_t * engine = ctx->engine;		// 从全局context中获取AIengine
			
			
			json_object * jresult = NULL;
			int rc = engine->predict(engine, frame, &jresult);
			if(rc == 0 && jresult)	// 如果能正常解析
			{
				const char * json_str = json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PLAIN);
				
				if(json_str)
				{
					int cb_json = strlen(json_str);
					// response中将json字符串返回给客户端
					soup_message_set_response(msg, "application/json",
						SOUP_MEMORY_COPY, json_str, cb_json);		
					soup_message_set_status(msg, SOUP_STATUS_OK);
				}else
				{
					rc = -1;
				}
			}
			if(jresult) json_object_put(jresult);
			input_frame_free(frame);
			if(0 == rc) return;
		}
		show_error_page(server, msg, fmt, 0, "TODO: ...");
		return;
	}

```

==========================================================================================

# IO-module的使用:

###step 1. 选择所使用的plugin类型		
		io_input_t ＊ input = io_input_init(NULL, "ioplugin::default", NULL);
	
###step 2. 初期化，加载配置 
		json_object * jconfig = json_object_from_file("input.json");
		input->init(input, jconfig);
	
###step 3.（可选）如果需要实时处理，设定新图像通知事件的回调函数
		input->on_new_frame = on_new_frame;
	
###step 4. （可选）启动。如果在客户端模式下希望自动获取图像，或是为了保障代码的一致性，
		input->run（input）；	// 客户端模式下有效，其他模式也可以调用，默认不做任何处理。

###step 5(可选): application終了前、手動でcleanup( C言語にresources手動release必要）
		io_input_cleanup(input);
	

# AI-engine的使用
###step 1. 选择所使用的plugin类型		
	ai_engine_t ＊ engine = ai_engine_init(NULL, "aiengine::default", NULL);

###step 2. 初期化，加载配置 
	json_object * jconfig = json_object_from_file("engine.json");
	engine->init(engine, jconfig);
	
###step 3. 获取当前最新的图像并解析
	□ 方式一(实时处理), 直接使用回调函数中的frame参数。
	(由于IO模块的回调函数使用了多线程，不能在GUI的程序中直接使用)
		static int on_new_frame(io_input_t * input, const input_frame_t * frame)
		{
			// frame: 当前最新的图像
			json_object * jresult = engine->predict(engine, frame);
			
			// UIにjresultを提供する
			
			json_object_put(jresult); // unref json object
			return 0;
		}
		
	□　方式二，手動でIOmoduleから获取当前最新的图像(在应用程序循环体内部或是在GUI程序的定时器中使用)
		input_frame_t frame[1] = { .data = NULL };
		long frame_number = input->get_frame(input, frame);
		json_object* jresult = engine->predict(engine, frame);
		
		// UIにjresultを提供する
		
		json_object_put(jresult); 	// unref json object
		input_frame_clear(frame);	//手動で获取图像の場合は、手動でcleanup
	
	□　方式三，不使用IOmodule，直接使用从任意输入源获取的图像。例如
		// unsigned char * jpeg_data = NULL;
		// ssize_t cb_image = load_binary_data（"1.jpg", &jpeg_data);
		// input_frame_t frame[1] = { .data = NULL };
		// char * meta_data = NULL； 	// (可选)，附加图像相关数据，比如json格式的annotations
		// int meta_length = 0; 		// = meta_data?strlen(meta_data):0;
		
		// built-in模块支持set_jpeg, set_png and set_bgra
		input_frame_set_jpeg(frame, jpeg_data, cb_image, meta_data, meta_length); 
		// free(jpeg_data);	// 手動でfree memory
		
		json_object* jresult = engine->predict(engine, frame);
		
		// UIにjresultを提供する
		
		json_object_put(jresult); 	// unref json object
		input_frame_clear(frame);	//手動で获取图像の場合は、手動でcleanup
		

###step 4(可选):
		// application終了前、手動でcleanup( C言語にresources手動release必要）
		ai_engine_cleanup(engine);
	
