#define PURPLE_PLUGINS

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <plugin.h>
#include <version.h>
#include <blist.h>
#include <gtkblist.h>
#include <gtkconv.h>
#include <savedstatuses.h>
#include <server.h>
#include <account.h>
#include <gtkutils.h>
#include <pidginstock.h>
#include <conversation.h>
#include <gtkhotkey.h>
#include <util.h>

// index

enum item_type
{
  CONTACT,
  CHAT,
  STATUS,
  STATUS_PRIMITIVE,
  STATUS_SAVED,
  ACTION,
  MESSAGE
};

typedef struct _item
{
  uint refs;
  enum item_type type;
  PurpleStatusPrimitive primitive;
  gpointer data;
} item;

typedef struct _pair
{
  gchar* key;
  item* value;
} pair;

static void on_destroy_pair(gpointer data)
{
  pair* p = (pair*)data;
  g_free(p->key);
  if (--p->value->refs == 0)
    g_free(p->value);
  g_free(p);
}

static gint compare_pair(gconstpointer a, gconstpointer b, gpointer cookie)
{
  return g_utf8_collate(((pair*)a)->key, ((pair*)b)->key);
}

static void append_item(GSequence* index, const gchar* name, item* item)
{
  int i;
  gchar** parts = g_strsplit_set(name, " \t\v\n\r\f", 0);
  for(i = 0; parts[i]; ++i)
  {
    pair* p = g_new(pair, 1);
    p->key = g_utf8_casefold(parts[i], -1);
    p->value = item;
    ++item->refs;
    g_sequence_append(index, p);
  }
  g_strfreev(parts);
}

static GSequence* create_index()
{
  GList* statuses;
  GList* accounts;
  GList* cur;
  PurpleBlistNode* node;
  int i;
  GSequence* result = g_sequence_new(on_destroy_pair);
  for(node = purple_blist_get_root(); node; node = purple_blist_node_next(node, TRUE))
  {
    const gchar* alias;
    item *val = g_new0(item, 1);
    val->data = node;
    switch(node->type)
    {
    case PURPLE_BLIST_CONTACT_NODE:
      alias = purple_contact_get_alias((PurpleContact*)node);
      val->type = CONTACT;
      break;
    case PURPLE_BLIST_CHAT_NODE:
      alias = ((PurpleChat*)node)->alias;
      val->type = CHAT;
      break;
    default:
      continue;
    }
    append_item(result, alias, val);
  }
  for (statuses = purple_savedstatuses_get_all(); statuses; statuses = statuses->next)
  {
    PurpleSavedStatus* sst = (PurpleSavedStatus*)statuses->data;
    if (!purple_savedstatus_is_transient(sst) ||
        purple_savedstatus_get_message(sst))
    {
      item *val = g_new0(item, 1);
      val->type = STATUS_SAVED;
      val->data = sst;
      append_item(result, purple_savedstatus_get_title(sst), val);
    }
  }
  for (i = 1; i < PURPLE_STATUS_NUM_PRIMITIVES; ++i)
  {
    item *val = g_new0(item, 1);
    val->type = STATUS_PRIMITIVE;
    val->primitive = i;
    append_item(result, purple_primitive_get_id_from_type(i), val);
    append_item(result, purple_primitive_get_name_from_type(i), val);
  }
  accounts = purple_accounts_get_all_active();
  for (cur = accounts; cur; cur = cur->next)
  {
    PurpleAccount* acct = (PurpleAccount*)cur->data;
    PurplePresence* pr = purple_account_get_presence(acct);
    GList* stl = purple_presence_get_statuses(pr);
    for(; stl; stl = stl->next)
    {
      PurpleStatus* st = (PurpleStatus*)stl->data;
      item *val = g_new0(item, 1);
      val->type = STATUS;
      val->data = st;
      append_item(result, purple_account_get_username(acct), val);
      append_item(result, purple_account_get_protocol_name(acct), val);
      append_item(result, purple_status_get_name(st), val);
    }
  }
  g_list_free(accounts);
  g_sequence_sort(result, compare_pair, NULL);
  return result;
}

static GSList* search_index(GSequence* index, const gchar* str)
{
  GSList *result = NULL;
  if (str[0])
  {
    pair p = {g_utf8_casefold(str, -1), NULL};
    GSequenceIter* iter = g_sequence_search(index, &p, compare_pair, NULL);
    //moving backward
    for (; !g_sequence_iter_is_begin(iter); iter = g_sequence_iter_prev(iter))
    {
      pair* pp = (pair*)g_sequence_get(g_sequence_iter_prev(iter));
      if (g_strcmp0(pp->key, p.key))
        break;
    }
    //moving forward
    for (; !g_sequence_iter_is_end(iter); iter = g_sequence_iter_next(iter))
    {
      pair* pp = (pair*)g_sequence_get(iter);
      if (!g_str_has_prefix(pp->key, p.key))
        break;
      if (!result)
        result = g_slist_append(result, pp->value);
      else
      {
        GSList* pos = result;
        while(pos->data != pp->value)
        {
          if (pos->next)
            pos = pos->next;
          else
          {
            pos->next = g_slist_alloc();
            pos->next->data = pp->value;
          }
        }
      }
    }
    g_free(p.key);
  }
  return result;
}

// ui

static void item_activate(item* item)
{
  PurpleAccount* account;
  PurpleChat* chat;
  PurpleBuddy* buddy;
  PurpleConversation* conv;
  PurplePluginProtocolInfo *prpl_info;
  GHashTable* components; 
  const char* name;
  PurpleSavedStatus *saved;
  switch(item->type)
  {
    case CONTACT:
      buddy = purple_contact_get_priority_buddy((PurpleContact*)item->data);
      account = purple_buddy_get_account(buddy);
      name = purple_buddy_get_name(buddy);
      conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, name, account);
      if (!conv)
        conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, name);
      if (conv)
        pidgin_conv_present_conversation(conv);
      break;
    case CHAT:
      chat = (PurpleChat*)item->data;
      account = purple_chat_get_account(chat);
      prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(
          purple_find_prpl(purple_account_get_protocol_id(account)));
      components = purple_chat_get_components(chat);
      if (prpl_info && prpl_info->get_chat_name)
        name = prpl_info->get_chat_name(components);
      else
        name = purple_chat_get_name(chat);
      conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, name, account);
      if (conv)
        pidgin_conv_present_conversation(conv);
      serv_join_chat(purple_account_get_connection(account), components);
      break;
    case STATUS:
      purple_status_set_active((PurpleStatus*)item->data, TRUE);
      break;
    case STATUS_PRIMITIVE:
      saved = purple_savedstatus_find_transient_by_type_and_message(item->primitive, NULL);
      if (!saved)
        saved = purple_savedstatus_new(NULL, item->primitive);
      purple_savedstatus_activate(saved);
      break;      
    case STATUS_SAVED:
      purple_savedstatus_activate((PurpleSavedStatus*)item->data);
      break;
    case MESSAGE:
      conv = (PurpleConversation*)((PurpleConvMessage*)item->data)->conv;
      if (conv)
        pidgin_conv_present_conversation(conv);
      break;
    case ACTION:
      break;
  }
}

static gchar* item_get_text(item* item)
{
  PurpleStatus* status;
  PurpleAccount* account;
  char* msg;
  char* text;
  switch(item->type)
  {
    case CONTACT:
      return g_strdup(purple_contact_get_alias((PurpleContact*)item->data));
    case CHAT:
      return g_strdup(purple_chat_get_name((PurpleChat*)item->data));
    case STATUS:
      status = (PurpleStatus*)item->data;
      account = purple_presence_get_account(purple_status_get_presence(status));
      return g_strdup_printf("%s (%s, %s)",
          purple_status_get_name(status),
          purple_account_get_protocol_name(account),
          purple_account_get_username(account));
    case STATUS_PRIMITIVE:
      return g_strdup_printf("%s (%s)",
          purple_primitive_get_name_from_type(item->primitive),
          purple_primitive_get_id_from_type(item->primitive));
    case STATUS_SAVED:
      return g_strdup(purple_savedstatus_get_title((PurpleSavedStatus*)item->data));
    case MESSAGE:
      msg = purple_markup_strip_html(
        purple_conversation_message_get_message((PurpleConvMessage*)item->data));
      text = g_strdup_printf("<b>%s</b>: %s",
          ((PurpleConvMessage*)item->data)->alias, msg);
      g_free(msg);
      return text;
    case ACTION:
      break;
  }
  return NULL;
}

static GdkPixbuf* get_icon_for_primitive(
    PurpleStatusPrimitive primitive, GtkTreeView* tree)
{
  const char* stock = pidgin_stock_id_from_status_primitive(primitive);
	GtkIconSize size = gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_EXTRA_SMALL);
	return gtk_widget_render_icon((GtkWidget*)tree, stock, size, "GtkTreeView");
}

static GdkPixbuf* item_get_icon(item* item, GtkTreeView* tree)
{
  switch(item->type)
  {
    case CONTACT:
    case CHAT:
      return pidgin_blist_get_status_icon((PurpleBlistNode*)item->data,
          PIDGIN_STATUS_ICON_LARGE);
    case STATUS:
      return get_icon_for_primitive(
          purple_status_type_get_primitive(
            purple_status_get_type((PurpleStatus*)item->data)), tree);
    case STATUS_PRIMITIVE:
      return get_icon_for_primitive(item->primitive, tree);
    case STATUS_SAVED:
      return get_icon_for_primitive(
          purple_savedstatus_get_type((PurpleSavedStatus*)item->data), tree);
    case ACTION:
    case MESSAGE:
      break;
  }
  return NULL;
}

static GtkTreePath* get_selected_path(GtkTreeView* tree)
{
  GtkTreeSelection* sel = gtk_tree_view_get_selection(tree);
  GList* rows = gtk_tree_selection_get_selected_rows(sel, NULL);
  if (rows)
    return (GtkTreePath*)rows->data;
  return NULL;
}

static void on_row_activated(GtkTreeView* tree, GtkTreePath* path, 
    GtkTreeViewColumn* col, gpointer user_data)
{
  if (path)
  {
    GtkTreeModel* model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
      GValue value = {0, {{0}}};
      gtk_tree_model_get_value(model, &iter, 2, &value);
      item_activate((item*)g_value_get_pointer(&value));
      gtk_widget_destroy((GtkWidget*)user_data);
    }
  }
}

static void move_selection(GtkTreeView* tree, gboolean up)
{
  GtkTreeSelection* sel = gtk_tree_view_get_selection(tree);
  GtkTreePath* path = get_selected_path(tree);
  if (!path)
    path = gtk_tree_path_new_first();
  if (up)
  {
    if (gtk_tree_path_prev(path))
    {
      gtk_tree_selection_select_path(sel, path);
      gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
    }
  }
  else
  {
    gtk_tree_path_next(path);
    gtk_tree_selection_select_path(sel, path);
    gtk_tree_view_scroll_to_cell(tree, path, NULL, FALSE, 0, 0);
  }
}

static gboolean on_entry_key_pressed(GtkWidget* widget, 
    GdkEventKey* event, gpointer user_data)
{
  GtkTreeView* tree = (GtkTreeView*)g_object_get_data((GObject*)widget, "tree");
  if (event->keyval == GDK_KEY_Up ||
    ((event->state & GDK_CONTROL_MASK) && event->hardware_keycode == 0x2d))
  {
    move_selection(tree, TRUE);
    return TRUE;
  }
  else if (event->keyval == GDK_KEY_Down ||
      ((event->state & GDK_CONTROL_MASK) && event->hardware_keycode == 0x2c))
  {
    move_selection(tree, FALSE);
    return TRUE;
  }
  else if (event->keyval == GDK_KEY_Return)
  {
    GtkTreePath* path = get_selected_path(tree);
    gtk_tree_view_row_activated(tree, path, gtk_tree_view_get_column(tree, 0));
    return TRUE;
  }
  return FALSE;
} 

static void populate_tree(GtkTreeView* tree, GSList* list)
{
  GtkTreeSelection* sel;
  GtkTreeIter first;
  GtkListStore* model = 
    gtk_list_store_new(3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_POINTER);
  GSList* cur;
  for (cur = list; cur; cur = cur->next)
  {
    GtkTreeIter iter;
    item* i = (item*)cur->data;
    gchar* text = item_get_text(i);
    GdkPixbuf* pixbuf = item_get_icon(i, tree);
    gtk_list_store_append(model, &iter);
    gtk_list_store_set(model, &iter, 0, pixbuf, 1, text, 2, i, -1);
    if (pixbuf)
      g_object_unref(pixbuf);
    g_free(text);
  }
  gtk_tree_view_set_model(tree, (GtkTreeModel*)model);
  sel = gtk_tree_view_get_selection(tree);
  if (gtk_tree_model_get_iter_first((GtkTreeModel*)model, &first))
    gtk_tree_selection_select_iter(sel, &first);
}

static GSList* get_unread_messages()
{
  GSList* result = NULL;
  GList* convs = g_list_concat(
      pidgin_conversations_find_unseen_list(PURPLE_CONV_TYPE_IM,
        PIDGIN_UNSEEN_TEXT, FALSE, 0),
      pidgin_conversations_find_unseen_list(PURPLE_CONV_TYPE_CHAT,
        PIDGIN_UNSEEN_TEXT, FALSE, 0));
  GList* cur;
  for (cur = convs; cur; cur = cur->next)
  {
    GList* messages = purple_conversation_get_message_history(
        (PurpleConversation*)cur->data);
    item *val = g_new0(item, 1);
    val->type = MESSAGE;
    val->data = messages->data;
    result = g_slist_append(result, val);
  }
  g_list_free(convs);
  return result;
}

static void on_changed(GtkEntryBuffer* buffer, GSequence* index)
{
  const gchar* text = gtk_entry_buffer_get_text(buffer);
  GSList* list = search_index(index, text);
  GtkTreeView* tree = (GtkTreeView*)g_object_get_data((GObject*)buffer, "tree");
  populate_tree(tree, list);
  g_slist_free(list);
}

static void on_deleted(GtkEntryBuffer* buffer, guint pos, guint n_chars, gpointer user_data)
{
  on_changed(buffer, (GSequence*)user_data);
}

static void on_inserted(GtkEntryBuffer* buffer, guint pos, 
    gchar* chars, guint n_chars, gpointer user_data)
{
  on_changed(buffer, (GSequence*)user_data);
}

static gboolean on_win_key_pressed(GtkWidget* widget, 
    GdkEventKey* event, gpointer user_data)
{
  if (event->keyval == GDK_KEY_Escape)
  {
    gtk_widget_destroy(widget);
    return TRUE;
  }
  return FALSE;
}

static void on_destroy(GtkWidget* object, gpointer user_data)
{
  g_sequence_free((GSequence*)user_data);
}

static void create_ui(GSequence* index)
{
  GSList* messages;

  GtkWindow* win = (GtkWindow*)gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget* vbox = gtk_vbox_new(FALSE, 4);
  GtkEntry* entry = (GtkEntry*)gtk_entry_new();
  GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
  GtkTreeView* tree = (GtkTreeView*)gtk_tree_view_new();
  GtkTreeViewColumn* col = gtk_tree_view_column_new();
  GtkCellRenderer* icon_rend = gtk_cell_renderer_pixbuf_new();
  GtkCellRenderer* text_rend = gtk_cell_renderer_text_new();
  GtkEntryBuffer* buffer = gtk_entry_get_buffer(entry);
  gtk_window_set_title(win, "QuickPurple");
  gtk_window_set_type_hint(win, GDK_WINDOW_TYPE_HINT_DIALOG);
  gtk_widget_set_size_request((GtkWidget*)win, -1,256);
  gtk_window_set_position(win, GTK_WIN_POS_CENTER);
  g_signal_connect((GtkWidget*)win, "destroy", (GCallback)on_destroy, index);
  g_signal_connect((GtkWidget*)win, "key-press-event",
      (GCallback)on_win_key_pressed, NULL);
  g_object_set_data((GObject*)entry, "tree", tree);
  gtk_entry_set_width_chars(entry, 32);
  g_signal_connect((GtkWidget*)entry, "key-press-event",
      (GCallback)on_entry_key_pressed, NULL);
  g_signal_connect((GtkWidget*)buffer, "deleted-text", (GCallback)on_deleted, index);
  g_signal_connect((GtkWidget*)buffer, "inserted-text", (GCallback)on_inserted, index);
  g_object_set_data((GObject*)buffer, "tree", tree);
  gtk_scrolled_window_set_policy((GtkScrolledWindow*)scroll, 
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type((GtkScrolledWindow*)scroll, GTK_SHADOW_IN);
  gtk_tree_view_set_headers_visible(tree, FALSE);
  g_object_set(text_rend, "wrap-mode", PANGO_WRAP_WORD, "wrap-width", 400, NULL);
  gtk_tree_view_column_pack_start(col, icon_rend, FALSE);
  gtk_tree_view_column_add_attribute(col, icon_rend, "pixbuf", 0);
  gtk_tree_view_column_pack_start(col, text_rend, TRUE);
  gtk_tree_view_column_add_attribute(col, text_rend, "markup", 1);
  gtk_tree_view_append_column(tree, col);
  g_signal_connect((GtkWidget*)tree, "row-activated", (GCallback)on_row_activated, win);
  gtk_container_add((GtkContainer*)scroll, (GtkWidget*)tree);
  gtk_container_set_border_width((GtkContainer*)vbox, 4);
  gtk_box_pack_start((GtkBox*)vbox, (GtkWidget*)entry, FALSE, FALSE, 0);
  gtk_box_pack_start((GtkBox*)vbox, scroll, TRUE, TRUE, 0);
  gtk_container_add((GtkContainer*)win, (GtkWidget*)vbox);
  gtk_widget_show_all((GtkWidget*)win);

  messages = get_unread_messages();
  populate_tree(tree, messages);
  g_slist_free(messages);
}

// plugin related stuff

static void plugin_action_test_cb(PurplePluginAction *action)
{
  GSequence* index = create_index();
  create_ui(index);
}

static GList* plugin_actions(PurplePlugin* plugin, gpointer context)
{
  PurplePluginAction* action = purple_plugin_action_new("Quickpurple",
      plugin_action_test_cb);
  return g_list_append(NULL, action);
}

//Hotkey handling

static GtkHotkeyInfo* gtk_hotkey_info = NULL;

static void on_hotkey(GtkHotkeyInfo* info, guint event_time, gpointer user_data)
{
  plugin_action_test_cb(NULL);
}

static gboolean quickpurple_load(PurplePlugin* plugin)
{
  gtk_hotkey_info = gtk_hotkey_info_new("Quickpurple", "QuickpurpleShow",
      "<Control><Alt>I", NULL);
  if (gtk_hotkey_info && gtk_hotkey_info_bind(gtk_hotkey_info, NULL))
      g_signal_connect(gtk_hotkey_info, "activated", (GCallback)on_hotkey, NULL);
}

static gboolean quickpurple_unload(PurplePlugin* plugin)
{
  if (gtk_hotkey_info)
    gtk_hotkey_info_unbind(gtk_hotkey_info, NULL);
}

static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_STANDARD,
    NULL,
    0,
    NULL,
    PURPLE_PRIORITY_DEFAULT,

    "gtk-shashurup-quickpurple",
    "Quickpurple",
    "0.1",

    "Alternative Pidgin buddy list",          
    "Quickpurple plugin",          
    "George Kibardin <george.kibardin@gmail.com>",                          
    "http://helloworld.tld",
    
    quickpurple_load,
    quickpurple_unload,                          
    NULL,                          
                                   
    NULL,                          
    NULL,                          
    NULL,                        
    plugin_actions,                   
    NULL,                          
    NULL,                          
    NULL,                          
    NULL                           
};                               

static void init_plugin(PurplePlugin *plugin)
{
}

PURPLE_INIT_PLUGIN(hello_purple, init_plugin, info)
