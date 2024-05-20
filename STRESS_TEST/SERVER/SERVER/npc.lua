myid = 99999;

function set_uid(x)
   myid = x;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   set_count = -1;
   if (player_x == my_x) then
      if (player_y == my_y) then
         API_SendMessage(myid, player, "HELLO");
         set_count = 3;
      end
   end
   return set_count;
end

function event_npc_bye(player)
   API_SendMessage(myid, player, "BYE");
end