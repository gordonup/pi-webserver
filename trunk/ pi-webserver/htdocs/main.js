// This file is part of Mongoose project, http://code.google.com/p/mongoose
var rele_status = [0, 0, 0, 0, 0, 0, 0, 0];
var input_status = [0, 0, 0, 0, 0, 0, 0, 0];
var cmds = ["a", "b", "c", "d", "e", "f", "g", "h"];
var chat = {
	// Backend URL, string.
	// 'http://backend.address.com' or '' if backend is the same as frontend
	backendUrl : '',
	maxVisibleMessages : 10,
	errorMessageFadeOutTimeoutMs : 2000,
	errorMessageFadeOutTimer : null,
	lastMessageId : 0,
	getMessagesIntervalMs : 1000,
};

chat.normalizeText = function(text) {
	return text.replace('<', '&lt;').replace('>', '&gt;');
};

chat.refresh = function(data) {
	/*
	$.each(data, function(index, entry) {
		var row = $('<div>').addClass('message-row').appendTo('#mml');
		var timestamp = (new Date(entry.timestamp * 1000)).toLocaleTimeString();
		$('<span>').addClass('message-timestamp').html('[' + timestamp + ']').prependTo(row);
		$('<span>').addClass('message-user').addClass(entry.user ? '' : 'message-user-server').html(chat.normalizeText((entry.user || '[server]') + ':')).appendTo(row);
		$('<span>').addClass('message-text').addClass(entry.user ? '' : 'message-text-server').html(chat.normalizeText(entry.text)).appendTo(row);
		chat.lastMessageId = Math.max(chat.lastMessageId, entry.id) + 1;
	});

	// Keep only chat.maxVisibleMessages, delete older ones.
	while($('#mml').children().length > chat.maxVisibleMessages) {
		$('#mml div:first-child').remove();
	}
	*/
	// OK GOOD FOR DEBUG: console.log(data);
	console.log(data);
	if (typeof(data)=='undefined'){
		return;
	}
	$.each(data, function(index, entry) {
		// INCREMENT LAST_ID
		chat.lastMessageId = Math.max(chat.lastMessageId, entry.id) + 1;
		if (entry.user=="from_terminal"){
			for (i = 1; i < 9; i++){
				if (entry.text == cmds[i-1] || entry.text == cmds[i-1].toUpperCase()){
					switch(input_status[i-1]) {
					case 0:
						input_status[i - 1] = 1;
						break;
					case 1:
						input_status[i - 1] = 0;
						break;
					default:
					//code to be executed if n is different from case 1 and 2
				}
				}
			}
		}
	});
	
	chat.refresh_inputs();
};
chat.refresh_inputs = function(){
	var input = $(".input_table_col_el");
	for( i = 0; i < input.size(); i++) {
		for( j = 1; j < 9; j++){
		if(input[i].id.indexOf(j) != -1) {
			switch(input_status[j-1]) {
			case 0:
				input[i].innerHTML = "0";
				break;
			case 1:
				input[i].innerHTML = "1";
				break;
			default:
			//code to be executed if n is different from case 1 and 2
		}
		}
		
	}
	}
	
}
chat.getMessages = function() {
	$.ajax({
		dataType : 'jsonp',
		url : chat.backendUrl + '/ajax/get_messages',
		data : {
			last_id : chat.lastMessageId
		},
		success : chat.refresh,
		error : function() {
		},
	});
	window.setTimeout(chat.getMessages, chat.getMessagesIntervalMs);
};

chat.handleMenuItemClick = function(ev) {
	var input = ev.target;
	var ajax_data;
	var x;
	for( i = 1; i < 9; i++) {
		if(input.id.indexOf(i) != -1) {
			switch(rele_status[i-1]) {
				case 0:
					input.src = "img/Power_Button_States_On_Off_clip_art_small.png";
					rele_status[i - 1] = 1;
					ajax_data = cmds[i - 1];
					break;
				case 1:
					input.src = "img/Powerbutton_States_On_Off_clip_art_small.png";
					rele_status[i - 1] = 0;
					ajax_data = cmds[i - 1].toUpperCase();
					break;
				default:
				//code to be executed if n is different from case 1 and 2
			}
			x = i;
		}
	}

	alert(ajax_data);

	$.ajax({
		dataType : 'jsonp',
		url : chat.backendUrl + '/ajax/send_message',
		data : {
			text : ajax_data
		},
		success : function(ev) {
			// input.value = '';
			// input.disabled = false;
			// chat.getMessages();
		},
		error : function(ev) {
			chat.showError('Error sending message');
			// input.disabled = false;
		},
	});

	$('.menu-item').removeClass('menu-item-selected');
	// Deselect menu buttons
	$(this).addClass('menu-item-selected');
	// Select clicked button
	$('.main').addClass('hidden');
	// Hide all main windows
	$('#' + $(this).attr('name')).removeClass('hidden');
	// Show main window
};

chat.showError = function(message) {
	$('#error').html(message).fadeIn('fast');
	window.clearTimeout(chat.errorMessageFadeOutTimer);
	chat.errorMessageFadeOutTimer = window.setTimeout(function() {
		$('#error').fadeOut('slow');
	}, chat.errorMessageFadeOutTimeoutMs);
};

chat.handleMessageInput = function(ev) {
	var input = ev.target;
	if(ev.keyCode != 13 || !input.value)
		return;
	input.disabled = true;

	$.ajax({
		dataType : 'jsonp',
		url : chat.backendUrl + '/ajax/send_message',
		data : {
			text : input.value
		},
		success : function(ev) {
			input.value = '';
			input.disabled = false;
			chat.getMessages();
		},
		error : function(ev) {
			chat.showError('Error sending message');
			input.disabled = false;
		},
	});
};

$(document).ready(function() {
	//$('.menu-item').click(chat.handleMenuItemClick);
	$('.button_img').click(chat.handleMenuItemClick);
	$('.message-input').keypress(chat.handleMessageInput);
	chat.getMessages();
});

// vim:ts=2:sw=2:et
